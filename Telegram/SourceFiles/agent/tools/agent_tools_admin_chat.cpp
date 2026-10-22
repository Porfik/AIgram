/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "agent/agent_files.h"
#include "agent/tools/agent_tool_rights.h"
#include "agent/tools/agent_tool_table.h"

#include "api/api_invite_links.h"
#include "api/api_peer_photo.h"
#include "apiwrap.h"
#include "base/timer.h"
#include "base/unixtime.h"
#include "core/mime_type.h"
#include "data/data_channel.h"
#include "data/data_chat.h"
#include "data/data_peer.h"
#include "data/data_session.h"
#include "data/data_user.h"
#include "lang/lang_keys.h"
#include "main/main_session.h"
#include "rpl/rpl.h"
#include "ui/image/image.h"

#include <QtCore/QJsonArray>

namespace Agent::AdminChatTools {
namespace {

constexpr auto kMaxInviteLinks = 20;
constexpr auto kMaxLinkNameLength = 32;
constexpr auto kMaxUsageLimit = 99999;

[[nodiscard]] QString TimeText(TimeId t) {
	return base::unixtime::parse(t).toString(u"yyyy-MM-dd HH:mm"_q);
}

[[nodiscard]] QJsonObject LinkToJson(const Api::InviteLink &link) {
	auto item = QJsonObject();
	item.insert("link", link.link);
	if (!link.label.isEmpty()) {
		item.insert("name", link.label);
	}
	item.insert("permanent", QJsonValue(link.permanent));
	item.insert("revoked", QJsonValue(link.revoked));
	if (link.expireDate) {
		item.insert("expires", QJsonValue(TimeText(link.expireDate)));
	}
	item.insert("usage", QJsonValue(link.usage));
	if (link.usageLimit) {
		item.insert("usage_limit", QJsonValue(link.usageLimit));
	}
	if (link.requested) {
		item.insert("requested", QJsonValue(link.requested));
	}
	if (link.requestApproval) {
		item.insert("request_approval", QJsonValue(true));
	}
	return item;
}

// WHY: these APIs never report a failure, so without a watchdog a lost
// request would leave the tool call waiting forever.
[[nodiscard]] base::Timer *ArmConfirmTimer(not_null<PendingCall*> call) {
	const auto timer = call->lifetime.make_state<base::Timer>([=] {
		call->fail(u"Telegram did not confirm the change; check the chat"
			" settings."_q);
	});
	timer->callOnce(crl::time(20000));
	return timer;
}

[[nodiscard]] bool CanHaveInviteLink(not_null<PeerData*> peer) {
	if (const auto chat = peer->asChat()) {
		return chat->canHaveInviteLink();
	} else if (const auto channel = peer->asChannel()) {
		return channel->canHaveInviteLink();
	}
	return false;
}

void RunListInviteLinks(not_null<PendingCall*> call) {
	const auto session = call->session;
	const auto peer = call->peer;
	const auto revoked = call->input.value("revoked").toBool();
	session->api().inviteLinks().requestMoreLinks(
		peer,
		session->user(),
		0,
		QString(),
		revoked,
		crl::guard(call.get(), [=](Api::PeerInviteLinks links) {
			auto array = QJsonArray();
			for (const auto &link : links.links) {
				array.push_back(LinkToJson(link));
			}
			auto json = QJsonObject();
			json.insert("links", array);
			call->done(std::move(json));
		}));
}

struct LinkFields {
	std::optional<QString> name;
	std::optional<TimeId> expireDate;
	std::optional<int> usageLimit;
	std::optional<bool> requestApproval;
};

[[nodiscard]] bool AnyLinkField(const LinkFields &fields) {
	return fields.name
		|| fields.expireDate
		|| fields.usageLimit
		|| fields.requestApproval;
}

[[nodiscard]] std::optional<LinkFields> ParseLinkFields(
		const QJsonObject &input,
		QString &error) {
	auto result = LinkFields();
	const auto nameValue = input.value("name");
	if (!nameValue.isUndefined()) {
		if (!nameValue.isString()) {
			error = u"name must be a string."_q;
			return std::nullopt;
		}
		auto name = nameValue.toString().trimmed();
		if (name.size() > kMaxLinkNameLength) {
			error = u"name must be at most %1 characters."_q.arg(
				kMaxLinkNameLength);
			return std::nullopt;
		}
		result.name = name;
	}
	const auto expireValue = input.value("expire_at");
	if (expireValue.isDouble()) {
		error = u"expire_at must be a local date and time as YYYY-MM-DD"
			" HH:MM."_q;
		return std::nullopt;
	} else if (!expireValue.isUndefined() && !expireValue.isNull()) {
		result.expireDate = ParseLocalTime(expireValue, error);
		if (!error.isEmpty()) {
			return std::nullopt;
		}
	}
	const auto limitValue = input.value("usage_limit");
	if (!limitValue.isUndefined()) {
		if (!limitValue.isDouble()) {
			error = u"usage_limit must be a number from 1 to %1."_q.arg(
				kMaxUsageLimit);
			return std::nullopt;
		}
		const auto limit = int(limitValue.toDouble());
		if (limit < 1 || limit > kMaxUsageLimit) {
			error = u"usage_limit must be a number from 1 to %1."_q.arg(
				kMaxUsageLimit);
			return std::nullopt;
		}
		result.usageLimit = limit;
	}
	const auto approvalValue = input.value("request_approval");
	if (approvalValue.isBool()) {
		result.requestApproval = approvalValue.toBool();
	}
	return result;
}

void AddCreateLinkRows(Card &card, const LinkFields &fields) {
	if (fields.name) {
		card.rows.push_back({
			.label = tr::lng_agent_card_row_link_name(tr::now),
			.value = fields.name->isEmpty()
				? tr::lng_agent_card_value_none(tr::now)
				: *fields.name,
		});
	}
	if (fields.expireDate) {
		card.rows.push_back({
			.label = tr::lng_agent_card_row_expires(tr::now),
			.value = TimeText(*fields.expireDate),
		});
	}
	if (fields.usageLimit) {
		card.rows.push_back({
			.label = tr::lng_agent_card_row_usage_limit(tr::now),
			.value = QString::number(*fields.usageLimit),
		});
	}
	if (fields.requestApproval) {
		card.rows.push_back({
			.label = tr::lng_agent_card_row_approval(tr::now),
			.value = *fields.requestApproval
				? tr::lng_agent_card_value_yes(tr::now)
				: tr::lng_agent_card_value_no(tr::now),
		});
	}
}

void AddEditLinkRows(
		Card &card,
		const Api::InviteLink &was,
		const LinkFields &fields) {
	if (fields.name) {
		card.rows.push_back({
			.label = tr::lng_agent_card_row_link_name(tr::now),
			.value = fields.name->isEmpty()
				? tr::lng_agent_card_value_none(tr::now)
				: *fields.name,
			.previous = was.label,
		});
	}
	if (fields.expireDate) {
		card.rows.push_back({
			.label = tr::lng_agent_card_row_expires(tr::now),
			.value = TimeText(*fields.expireDate),
			.previous = was.expireDate
				? TimeText(was.expireDate)
				: tr::lng_agent_card_value_forever(tr::now),
		});
	}
	if (fields.usageLimit) {
		card.rows.push_back({
			.label = tr::lng_agent_card_row_usage_limit(tr::now),
			.value = QString::number(*fields.usageLimit),
			.previous = was.usageLimit
				? QString::number(was.usageLimit)
				: tr::lng_agent_card_value_no_limit(tr::now),
		});
	}
	if (fields.requestApproval) {
		card.rows.push_back({
			.label = tr::lng_agent_card_row_approval(tr::now),
			.value = *fields.requestApproval
				? tr::lng_agent_card_value_yes(tr::now)
				: tr::lng_agent_card_value_no(tr::now),
			.previous = was.requestApproval
				? tr::lng_agent_card_value_yes(tr::now)
				: tr::lng_agent_card_value_no(tr::now),
		});
	}
}

void RunCreateInviteLink(not_null<PendingCall*> call) {
	const auto session = call->session;
	const auto peer = call->peer;
	auto error = QString();
	const auto fields = ParseLinkFields(call->input, error);
	if (!fields) {
		call->fail(error);
		return;
	} else if (!CanHaveInviteLink(peer)) {
		call->fail(u"You need the invite_users admin right for this."_q);
		return;
	}
	auto card = Card{ .kind = CardKind::Change };
	card.text = tr::lng_agent_card_title_create_link(tr::now);
	card.detail = u"create_invite_link"_q;
	card.rows.push_back({
		.label = tr::lng_agent_card_row_chat(tr::now),
		.value = PeerTitle(peer),
	});
	AddCreateLinkRows(card, *fields);
	call->confirm(std::move(card), [=](Card decided) {
		const auto timer = ArmConfirmTimer(call);
		session->api().inviteLinks().create(Api::CreateInviteLinkArgs{
			.peer = peer,
			.done = crl::guard(call.get(), [=](Api::InviteLink link) {
				timer->cancel();
				auto json = QJsonObject();
				json.insert("link", LinkToJson(link));
				call->done(std::move(json));
			}),
			.label = fields->name.value_or(QString()),
			.expireDate = fields->expireDate.value_or(0),
			.usageLimit = fields->usageLimit.value_or(0),
			.requestApproval = fields->requestApproval.value_or(false),
		});
	});
}

void RunEditInviteLink(not_null<PendingCall*> call) {
	const auto session = call->session;
	const auto peer = call->peer;
	const auto linkText = call->input.value("link").toString();
	auto error = QString();
	const auto fields = ParseLinkFields(call->input, error);
	if (linkText.isEmpty()) {
		call->fail(u"link is required. List the links first to get one."_q);
		return;
	} else if (!fields) {
		call->fail(error);
		return;
	} else if (!AnyLinkField(*fields)) {
		call->fail(u"At least one link setting is required."_q);
		return;
	} else if (!CanHaveInviteLink(peer)) {
		call->fail(u"You need the invite_users admin right for this."_q);
		return;
	}
	session->api().inviteLinks().requestMoreLinks(
		peer,
		session->user(),
		0,
		QString(),
		false,
		crl::guard(call.get(), [=](Api::PeerInviteLinks links) {
			auto was = std::optional<Api::InviteLink>();
			for (const auto &row : links.links) {
				if (row.link == linkText) {
					was = row;
					break;
				}
			}
			if (!was) {
				call->fail(u"Unknown link: %1. Use a link from"
					" list_invite_links."_q.arg(linkText.left(64)));
				return;
			}
			auto card = Card{ .kind = CardKind::Change };
			card.text = tr::lng_agent_card_title_edit_link(tr::now);
			card.detail = u"edit_invite_link"_q;
			card.rows.push_back({
				.label = tr::lng_agent_card_row_chat(tr::now),
				.value = PeerTitle(peer),
			});
			AddEditLinkRows(card, *was, *fields);
			call->confirm(std::move(card), [=](Card decided) {
				const auto timer = ArmConfirmTimer(call);
				session->api().inviteLinks().edit(
					peer,
					session->user(),
					linkText,
					fields->name.value_or(was->label),
					fields->expireDate.value_or(was->expireDate),
					fields->usageLimit.value_or(was->usageLimit),
					fields->requestApproval.value_or(was->requestApproval),
					crl::guard(call.get(), [=](Api::InviteLink link) {
						timer->cancel();
						auto json = QJsonObject();
						json.insert("link", LinkToJson(link));
						call->done(std::move(json));
					}));
			});
		}));
}

void RunRevokeInviteLinks(not_null<PendingCall*> call) {
	const auto session = call->session;
	const auto peer = call->peer;
	const auto value = call->input.value("links");
	if (!value.isArray()) {
		call->fail(u"links must be an array of invite link strings."_q);
		return;
	}
	const auto list = value.toArray();
	if (list.isEmpty() || list.size() > kMaxInviteLinks) {
		call->fail(u"links must contain 1 to %1 links."_q.arg(kMaxInviteLinks));
		return;
	} else if (!CanHaveInviteLink(peer)) {
		call->fail(u"You need the invite_users admin right for this."_q);
		return;
	}
	session->api().inviteLinks().requestMoreLinks(
		peer,
		session->user(),
		0,
		QString(),
		false,
		crl::guard(call.get(), [=](Api::PeerInviteLinks links) {
			auto wanted = std::vector<Api::InviteLink>();
			for (const auto &item : list) {
				const auto text = item.toString();
				const auto i = ranges::find(
					links.links,
					text,
					&Api::InviteLink::link);
				if (i == end(links.links)) {
					call->fail(u"Unknown link: %1. Use links from"
						" list_invite_links."_q.arg(text.left(64)));
					return;
				}
				wanted.push_back(*i);
			}
			auto card = Card{ .kind = CardKind::Change };
			card.text = tr::lng_agent_card_title_revoke_links(tr::now);
			card.detail = u"revoke_invite_links"_q;
			card.rows.push_back({
				.label = tr::lng_agent_card_row_chat(tr::now),
				.value = PeerTitle(peer),
			});
			for (const auto &link : wanted) {
				card.targets.push_back(CardTarget{
					.peer = peer->id.value,
					.title = link.link,
				});
			}
			call->confirm(std::move(card), [=](Card decided) {
				const auto timer = ArmConfirmTimer(call);
				ForEachTarget(call, std::move(decided), [=](
						const CardTarget &target,
						Fn<void(QString error)> done) {
					const auto i = ranges::find(
						wanted,
						target.title,
						&Api::InviteLink::link);
					if (i == end(wanted)) {
						done(u"The link was already revoked."_q);
						return;
					}
					const auto finished = crl::guard(call.get(), [=](
							const Api::InviteLink &) {
						done(QString());
					});
					if (i->permanent) {
						session->api().inviteLinks().revokePermanent(
							peer,
							session->user(),
							i->link,
							crl::guard(call.get(), [=] {
								done(QString());
							}));
					} else {
						session->api().inviteLinks().revoke(
							peer,
							session->user(),
							i->link,
							finished);
					}
				}, [=](Card finished) {
					timer->cancel();
					call->finishCard(std::move(finished));
				});
			});
		}));
}

void RunEditChatInfoLoaded(not_null<PendingCall*> call);

void RunEditChatInfo(not_null<PendingCall*> call) {
	WhenFullLoaded(call, call->peer, [=] {
		RunEditChatInfoLoaded(call);
	});
}

void RunEditChatInfoLoaded(not_null<PendingCall*> call) {
	const auto session = call->session;
	const auto peer = call->peer;
	const auto titleValue = call->input.value("title");
	const auto descriptionValue = call->input.value("description");
	if (titleValue.isUndefined() && descriptionValue.isUndefined()) {
		call->fail(u"At least one of title and description is required."_q);
		return;
	}
	const auto title = titleValue.toString().trimmed();
	const auto description = descriptionValue.toString().trimmed();
	if (!titleValue.isUndefined() && title.isEmpty()) {
		call->fail(u"title must not be empty."_q);
		return;
	} else if (title.size() > 128) {
		call->fail(u"title must be at most 128 characters."_q);
		return;
	} else if (description.size() > 255) {
		call->fail(u"description must be at most 255 characters."_q);
		return;
	}
	auto card = Card{ .kind = CardKind::Change };
	card.text = tr::lng_agent_card_title_edit_chat_info(tr::now);
	card.detail = u"edit_chat_info"_q;
	card.rows.push_back({
		.label = tr::lng_agent_card_row_chat(tr::now),
		.value = PeerTitle(peer),
	});
	if (!titleValue.isUndefined()) {
		card.rows.push_back({
			.label = tr::lng_agent_card_row_title(tr::now),
			.value = title,
			.previous = peer->name(),
		});
	}
	if (!descriptionValue.isUndefined()) {
		card.rows.push_back({
			.label = tr::lng_agent_card_row_description(tr::now),
			.value = description,
			.previous = peer->about(),
		});
	}
	call->confirm(std::move(card), [=](Card decided) {
		const auto savedDescription = descriptionValue.isUndefined()
			? std::optional<QString>()
			: description;
		const auto finish = [=] {
			if (savedDescription) {
				peer->setAbout(*savedDescription);
			}
			auto final = decided;
			final.status = CardStatus::Done;
			call->finishCard(std::move(final));
		};
		const auto saveDescription = [=] {
			call->api.request(MTPmessages_EditChatAbout(
				peer->input(),
				MTP_string(*savedDescription)
			)).done([=] {
				finish();
			}).fail([=](const MTP::Error &error) {
				if (error.type() == u"CHAT_ABOUT_NOT_MODIFIED"_q) {
					finish();
					return;
				}
				call->fail(MtpErrorText(error));
			}).send();
		};
		const auto saveTitle = [=] {
			if (const auto channel = peer->asChannel()) {
				call->api.request(MTPchannels_EditTitle(
					channel->inputChannel(),
					MTP_string(title)
				)).done([=](const MTPUpdates &result) {
					session->api().applyUpdates(result);
					if (savedDescription) {
						saveDescription();
					} else {
						auto final = decided;
						final.status = CardStatus::Done;
						call->finishCard(std::move(final));
					}
				}).fail([=](const MTP::Error &error) {
					call->fail(MtpErrorText(error));
				}).send();
			} else if (const auto chat = peer->asChat()) {
				call->api.request(MTPmessages_EditChatTitle(
					chat->inputChat(),
					MTP_string(title)
				)).done([=](const MTPUpdates &result) {
					session->api().applyUpdates(result);
					if (savedDescription) {
						saveDescription();
					} else {
						auto final = decided;
						final.status = CardStatus::Done;
						call->finishCard(std::move(final));
					}
				}).fail([=](const MTP::Error &error) {
					call->fail(MtpErrorText(error));
				}).send();
			} else {
				Unexpected("Peer in RunEditChatInfo.");
			}
		};
		if (titleValue.isUndefined()) {
			saveDescription();
		} else {
			saveTitle();
		}
	});
}

void RunSetChatPhoto(not_null<PendingCall*> call) {
	const auto session = call->session;
	const auto peer = call->peer;
	const auto attachmentValue = call->input.value("attachment");
	const auto fileValue = call->input.value("file");
	const auto remove = call->input.value("remove").toBool();
	const auto hasAttachment = !attachmentValue.isUndefined();
	const auto hasFile = !fileValue.isUndefined();
	const auto count = int(hasAttachment) + int(hasFile) + int(remove);
	if (count != 1) {
		call->fail(u"Pass exactly one of attachment, file and remove."_q);
		return;
	}
	if (remove) {
		auto card = Card{ .kind = CardKind::Change };
		card.text = tr::lng_agent_card_title_remove_chat_photo(tr::now);
		card.detail = u"set_chat_photo"_q;
		card.rows.push_back({
			.label = tr::lng_agent_card_row_chat(tr::now),
			.value = PeerTitle(peer),
		});
		call->confirm(std::move(card), [=](Card decided) {
			const auto applier = [=](const MTPUpdates &result) {
				auto final = decided;
				session->api().applyUpdates(result);
				final.status = CardStatus::Done;
				call->finishCard(std::move(final));
			};
			if (const auto chat = peer->asChat()) {
				call->api.request(MTPmessages_EditChatPhoto(
					chat->inputChat(),
					MTP_inputChatPhotoEmpty()
				)).done(applier).fail([=](const MTP::Error &error) {
					call->fail(MtpErrorText(error));
				}).send();
			} else if (const auto channel = peer->asChannel()) {
				call->api.request(MTPchannels_EditPhoto(
					channel->inputChannel(),
					MTP_inputChatPhotoEmpty()
				)).done(applier).fail([=](const MTP::Error &error) {
					call->fail(MtpErrorText(error));
				}).send();
			} else {
				Unexpected("Peer in RunSetChatPhoto.");
			}
		});
		return;
	}
	auto ref = QString();
	auto name = QString();
	auto mimeType = QString();
	auto size = qint64(0);
	auto path = QString();
	auto bytes = QByteArray();
	if (hasAttachment) {
		ref = attachmentValue.toString();
		const auto block = call->readAttachment(ref);
		if (!block) {
			call->fail(u"There is no attachment %1 in this session."_q.arg(ref));
			return;
		}
		bytes = block->bytes;
		name = block->name;
		mimeType = block->mimeType;
		size = block->size;
	} else {
		ref = fileValue.toString();
		path = ResolveAgentFile(session, ref);
		if (path.isEmpty()) {
			call->fail(u"%1 is not a file in the agent folder."_q.arg(ref));
			return;
		}
		const auto info = QFileInfo(path);
		name = info.fileName();
		mimeType = Core::MimeTypeForFile(info).name();
		size = info.size();
	}
	const auto image = Images::Read({
		.path = path,
		.content = bytes,
	}).image;
	if (image.isNull()) {
		call->fail(u"The file is not an image."_q);
		return;
	}
	auto card = Card{ .kind = CardKind::Change };
	card.text = tr::lng_agent_card_title_chat_photo(tr::now);
	card.detail = u"set_chat_photo"_q;
	card.rows.push_back({
		.label = tr::lng_agent_card_row_chat(tr::now),
		.value = PeerTitle(peer),
	});
	card.files.push_back(CardFile{
		.ref = ref,
		.name = name,
		.mimeType = mimeType,
		.size = size,
	});
	call->confirm(std::move(card), [=, image = image](Card decided) {
		const auto timer = ArmConfirmTimer(call);
		session->api().peerPhoto().uploadFailed(
		) | rpl::filter([=](not_null<PeerData*> failed) {
			return failed == peer;
		}) | rpl::on_next([=] {
			call->fail(u"Uploading the chat photo failed."_q);
		}, call->lifetime);
		session->api().peerPhoto().upload(
			peer,
			{ .image = image },
			crl::guard(call.get(), [=]() mutable {
				timer->cancel();
				decided.status = CardStatus::Done;
				call->finishCard(std::move(decided));
			}));
	});
}

[[nodiscard]] QString SlowmodeText(int seconds) {
	return !seconds
		? tr::lng_rights_slowmode_off(tr::now)
		: (seconds < 60)
		? tr::lng_seconds_tiny(tr::now, lt_count, seconds)
		: (seconds < 3600)
		? tr::lng_minutes_tiny(tr::now, lt_count, seconds / 60)
		: tr::lng_hours_tiny(tr::now, lt_count, seconds / 3600);
}

void RunSetChatPermissions(not_null<PendingCall*> call) {
	const auto session = call->session;
	const auto peer = call->peer;
	const auto deniedValue = call->input.value("denied");
	const auto slowValue = call->input.value("slow_mode_seconds");
	const auto hasDenied = !deniedValue.isUndefined();
	const auto hasSlow = !slowValue.isUndefined();
	if (!hasDenied && !hasSlow) {
		call->fail(u"At least one of denied and slow_mode_seconds is"
			" required."_q);
		return;
	}
	auto error = QString();
	auto denied = std::optional<ChatRestrictions>();
	if (hasDenied) {
		denied = ToolRights::ParseRestrictions(deniedValue, error);
		if (!denied) {
			call->fail(error);
			return;
		} else if (ToolRights::RestrictionNames(*denied).contains(
				u"view_messages"_q)) {
			call->fail(u"view_messages can't be denied to all members."_q);
			return;
		}
	}
	auto slowMode = std::optional<int>();
	if (hasSlow) {
		if (!slowValue.isDouble()) {
			call->fail(u"slow_mode_seconds must be one of 0, 10, 30, 60,"
				" 300, 900 or 3600."_q);
			return;
		}
		slowMode = int(slowValue.toDouble());
		constexpr std::array<int, 7> kSlowModes = {
			0, 10, 30, 60, 300, 900, 3600,
		};
		if (!ranges::contains(kSlowModes, *slowMode)) {
			call->fail(u"slow_mode_seconds must be one of 0, 10, 30, 60,"
				" 300, 900 or 3600."_q);
			return;
		} else if (*slowMode && !peer->isChannel()) {
			call->fail(u"Slow mode works only in supergroups."_q);
			return;
		}
	}
	const auto current = peer->isChannel()
		? peer->asChannel()->defaultRestrictions()
		: peer->asChat()
		? peer->asChat()->defaultRestrictions()
		: ChatRestrictions();
	auto card = Card{ .kind = CardKind::Change };
	card.text = tr::lng_agent_card_title_chat_permissions(tr::now);
	card.detail = u"set_chat_permissions"_q;
	card.rows.push_back({
		.label = tr::lng_agent_card_row_chat(tr::now),
		.value = PeerTitle(peer),
	});
	if (hasDenied) {
		const auto names = ToolRights::RestrictionNames(*denied);
		card.rows.push_back({
			.label = tr::lng_agent_card_row_denied(tr::now),
			.value = names.isEmpty()
				? tr::lng_agent_card_value_nothing(tr::now)
				: names.join(u", "_q),
			.previous = [&] {
				const auto was = ToolRights::RestrictionNames(current);
				return was.isEmpty()
					? tr::lng_agent_card_value_nothing(tr::now)
					: was.join(u", "_q);
			}(),
		});
	}
	if (hasSlow) {
		card.rows.push_back({
			.label = tr::lng_agent_card_row_slow_mode(tr::now),
			.value = SlowmodeText(*slowMode),
			.previous = peer->isChannel()
				? SlowmodeText(peer->asChannel()->slowmodeSeconds())
				: SlowmodeText(0),
		});
	}
	call->confirm(std::move(card), [=](Card decided) {
		const auto finish = [=] {
			auto final = decided;
			final.status = CardStatus::Done;
			call->finishCard(std::move(final));
		};
		const auto saveSlow = [=] {
			if (!hasSlow) {
				finish();
				return;
			}
			call->api.request(MTPchannels_ToggleSlowMode(
				peer->asChannel()->inputChannel(),
				MTP_int(*slowMode)
			)).done([=](const MTPUpdates &result) {
				session->api().applyUpdates(result);
				peer->asChannel()->setSlowmodeSeconds(*slowMode);
				finish();
			}).fail([=](const MTP::Error &error) {
				call->fail(MtpErrorText(error));
			}).send();
		};
		const auto saveDenied = [=] {
			if (!hasDenied) {
				saveSlow();
				return;
			}
			call->api.request(MTPmessages_EditChatDefaultBannedRights(
				peer->input(),
				RestrictionsToMTP({ *denied, 0 })
			)).done([=](const MTPUpdates &result) {
				session->api().applyUpdates(result);
				if (const auto chat = peer->asChat()) {
					chat->setDefaultRestrictions(*denied);
				} else if (const auto channel = peer->asChannel()) {
					channel->setDefaultRestrictions(*denied);
				}
				saveSlow();
			}).fail([=](const MTP::Error &error) {
				call->fail(MtpErrorText(error));
			}).send();
		};
		saveDenied();
	});
}

void InviteChatUsers(
		not_null<PendingCall*> call,
		not_null<ChannelData*> channel,
		std::vector<not_null<UserData*>> users,
		int index,
		const std::shared_ptr<QJsonArray> &invited,
		const std::shared_ptr<QJsonArray> &failed,
		Fn<void()> finish) {
	if (index == int(users.size())) {
		finish();
		return;
	}
	const auto session = call->session;
	const auto user = users[index];
	const auto next = [=] {
		InviteChatUsers(
			call,
			channel,
			users,
			index + 1,
			invited,
			failed,
			finish);
	};
	call->api.request(MTPchannels_InviteToChannel(
		channel->inputChannel(),
		MTP_vector<MTPInputUser>(1, user->inputUser())
	)).done([=](const MTPmessages_invitedUsers &result) {
		session->api().applyUpdates(result.data().vupdates());
		invited->push_back(QJsonValue(qint64(user->id.value)));
		next();
	}).fail([=](const MTP::Error &error) {
		const auto text = MtpErrorText(error);
		auto item = QJsonObject();
		item.insert("id", QJsonValue(qint64(user->id.value)));
		item.insert("error", text);
		failed->push_back(item);
		const auto stop = error.type().startsWith(u"FLOOD_WAIT"_q)
			|| (error.type() == u"PEER_FLOOD"_q);
		if (stop) {
			for (auto i = index + 1; i < int(users.size()); ++i) {
				auto rest = QJsonObject();
				rest.insert("id", QJsonValue(qint64(users[i]->id.value)));
				rest.insert("error", text);
				failed->push_back(rest);
			}
			finish();
			return;
		}
		next();
	}).send();
}

void RunCreateChat(not_null<PendingCall*> call) {
	const auto session = call->session;
	const auto type = call->input.value("type").toString();
	const auto title = call->input.value("title").toString().trimmed();
	const auto description = call->input.value("description").toString();
	if (type != u"group"_q && type != u"channel"_q) {
		call->fail(u"type must be \"group\" or \"channel\"."_q);
		return;
	} else if (title.isEmpty()) {
		call->fail(u"title must not be empty."_q);
		return;
	} else if (title.size() > 128) {
		call->fail(u"title must be at most 128 characters."_q);
		return;
	} else if (description.size() > 255) {
		call->fail(u"description must be at most 255 characters."_q);
		return;
	}
	auto error = QString();
	const auto usersValue = call->input.value("users");
	auto users = (usersValue.isUndefined() || usersValue.isNull())
		? std::vector<not_null<UserData*>>()
		: ResolveUserRefs(call, usersValue, 50, error);
	if (!error.isEmpty()) {
		call->fail(error);
		return;
	}
	users.erase(
		ranges::remove_if(users, &UserData::isSelf),
		end(users));
	auto card = Card{ .kind = CardKind::Change };
	card.text = (type == u"group"_q)
		? tr::lng_agent_card_title_create_group(tr::now)
		: tr::lng_agent_card_title_create_channel(tr::now);
	card.detail = u"create_chat"_q;
	card.rows.push_back({
		.label = tr::lng_agent_card_row_title(tr::now),
		.value = title,
	});
	if (!description.isEmpty()) {
		card.rows.push_back({
			.label = tr::lng_agent_card_row_description(tr::now),
			.value = description,
		});
	}
	if (!users.empty()) {
		auto names = QStringList();
		for (const auto user : users) {
			names.push_back(PeerTitle(&*user));
		}
		card.rows.push_back({
			.label = tr::lng_agent_card_row_members(tr::now),
			.value = names.join(u", "_q),
		});
	}
	call->confirm(std::move(card), [=](Card decided) {
		const auto megagroup = (type == u"group"_q);
		using Flag = MTPchannels_CreateChannel::Flag;
		const auto flags = megagroup
			? Flag::f_megagroup
			: Flag::f_broadcast;
		call->api.request(MTPchannels_CreateChannel(
			MTP_flags(flags),
			MTP_string(title),
			MTP_string(description),
			MTPInputGeoPoint(),
			MTPstring(),
			MTP_int(0)
		)).done([=](const MTPUpdates &result) {
			session->api().applyUpdates(result);
			const QVector<MTPChat> *chats = nullptr;
			switch (result.type()) {
			case mtpc_updates:
				chats = &result.c_updates().vchats().v;
				break;
			case mtpc_updatesCombined:
				chats = &result.c_updatesCombined().vchats().v;
				break;
			}
			auto channel = (ChannelData*)nullptr;
			if (chats) {
				for (const auto &chat : *chats) {
					if (chat.type() == mtpc_channel) {
						channel = session->data().channel(
							chat.c_channel().vid());
					}
				}
			}
			if (!channel) {
				call->fail(u"Could not create the chat."_q);
				return;
			}
			if (users.empty()) {
				auto chat = QJsonObject();
				chat.insert("id", QJsonValue(qint64(channel->id.value)));
				chat.insert("title", channel->name());
				auto json = QJsonObject();
				json.insert("chat", chat);
				json.insert("invited", QJsonArray());
				json.insert("failed", QJsonArray());
				call->done(std::move(json));
				return;
			}
			const auto invited = std::make_shared<QJsonArray>();
			const auto failed = std::make_shared<QJsonArray>();
			const auto finishJson = [=] {
				auto chat = QJsonObject();
				chat.insert("id", QJsonValue(qint64(channel->id.value)));
				chat.insert("title", channel->name());
				auto json = QJsonObject();
				json.insert("chat", chat);
				json.insert("invited", *invited);
				json.insert("failed", *failed);
				call->done(std::move(json));
			};
			InviteChatUsers(call, channel, users, 0, invited, failed, finishJson);
		}).fail([=](const MTP::Error &error) {
			call->fail(MtpErrorText(error));
		}).send();
	});
}

[[nodiscard]] PeerData *JoinResultPeer(
		not_null<Main::Session*> session,
		int64 bareId) {
	if (const auto chat = session->data().chatLoaded(ChatId(bareId))) {
		return chat;
	}
	return session->data().channelLoaded(ChannelId(bareId));
}

void RunJoinChat(not_null<PendingCall*> call) {
	const auto session = call->session;
	const auto link = call->input.value("link").toString().trimmed();
	if (link.isEmpty()) {
		call->fail(u"link is required. Use an invite link or an @username."_q);
		return;
	}
	auto text = link;
	auto lower = text.toLower();
	if (lower.startsWith(u"https://"_q)) {
		text = text.mid(8);
	} else if (lower.startsWith(u"http://"_q)) {
		text = text.mid(7);
	} else if (lower.startsWith(u"tg://join?invite="_q)) {
		text = text.mid(17);
	}
	lower = text.toLower();
	auto hash = QString();
	auto username = QString();
	if (lower.startsWith(u"t.me/+"_q) || lower.startsWith(u"telegram.me/+"_q)) {
		hash = text.mid(text.indexOf('+') + 1);
	} else if (lower.startsWith(u"t.me/joinchat/"_q)
		|| lower.startsWith(u"telegram.me/joinchat/"_q)) {
		hash = text.mid(text.indexOf(u"/joinchat/"_q) + 11);
	} else if (lower.startsWith(u"t.me/"_q)) {
		username = text.mid(5);
	} else if (lower.startsWith(u"telegram.me/"_q)) {
		username = text.mid(12);
	} else if (text.startsWith('@')) {
		username = text.mid(1);
	} else if (!lower.startsWith(u"join?invite="_q)) {
		hash = text;
	}
	if (!hash.isEmpty()) {
		if (hash.contains('/')) {
			hash = hash.mid(hash.lastIndexOf('/') + 1);
		}
		call->api.request(MTPmessages_CheckChatInvite(
			MTP_string(hash)
		)).done(crl::guard(call.get(), [=](const MTPChatInvite &result) {
			if (result.type() == mtpc_chatInviteAlready) {
				auto bare = int64(0);
				result.c_chatInviteAlready().vchat().match([&](
						const MTPDchat &data) { bare = data.vid().v; }, [&](
						const MTPDchatForbidden &data) { bare = data.vid().v; },
					[&](const MTPDchannel &data) { bare = data.vid().v; }, [&](
						const MTPDchannelForbidden &data) { bare = data.vid().v; },
					[&](const auto &) {});
				session->data().processChats(MTP_vector<MTPChat>(
					1,
					result.c_chatInviteAlready().vchat()));
				const auto peer = JoinResultPeer(session, bare);
				call->fail(u"Already a member of chat %1."_q.arg(
					peer ? peer->id.value : bare));
				return;
			}
			const auto &data = result.c_chatInvite();
			const auto title = qs(data.vtitle());
			const auto count = data.vparticipants_count().v;
			const auto about = qs(data.vabout().value_or_empty());
			const auto requestNeeded = data.is_request_needed();
			auto card = Card{ .kind = CardKind::Change };
			card.text = tr::lng_agent_card_title_join_chat(tr::now);
			card.detail = u"join_chat"_q;
			card.rows.push_back({
				.label = tr::lng_agent_card_row_chat(tr::now),
				.value = title,
			});
			if (count) {
				card.rows.push_back({
					.label = tr::lng_agent_card_row_members(tr::now),
					.value = QString::number(count),
				});
			}
			if (!about.isEmpty()) {
				card.rows.push_back({
					.label = tr::lng_agent_card_row_description(tr::now),
					.value = about,
				});
			}
			call->confirm(std::move(card), [=](Card decided) {
				call->api.request(MTPmessages_ImportChatInvite(
					MTP_string(hash)
				)).done([=](const MTPmessages_ChatInviteJoinResult &result) {
					if (result.type()
						== mtpc_messages_chatInviteJoinResultWebView) {
						call->fail(u"This invite can only be joined in its"
							" app."_q);
						return;
					}
					auto chatId = int64(0);
					const auto &updates = result.c_messages_chatInviteJoinResultOk().vupdates();
					if (updates.type() == mtpc_updates) {
						for (const auto &chat : updates.c_updates().vchats().v) {
							if (chat.type() == mtpc_chat) {
								chatId = chat.c_chat().vid().v;
							} else if (chat.type() == mtpc_channel) {
								chatId = chat.c_channel().vid().v;
							}
						}
					}
					session->api().applyUpdates(updates);
					const auto peer = JoinResultPeer(session, chatId);
					auto chat = QJsonObject();
					chat.insert("id", QJsonValue(qint64(
						peer ? peer->id.value : chatId)));
					chat.insert("title", peer ? peer->name() : title);
					auto json = QJsonObject();
					json.insert("chat", chat);
					json.insert("joined", QJsonValue(true));
					call->done(std::move(json));
				}).fail([=](const MTP::Error &error) {
					if (error.type() == u"INVITE_REQUEST_SENT"_q) {
						auto chat = QJsonObject();
						chat.insert("id", QJsonValue(0));
						chat.insert("title", title);
						auto json = QJsonObject();
						json.insert("chat", chat);
						json.insert("requested", QJsonValue(true));
						call->done(std::move(json));
						return;
					}
					call->fail(MtpErrorText(error));
				}).send();
			});
		})).fail(crl::guard(call.get(), [=](const MTP::Error &error) {
			call->fail(MtpErrorText(error));
		})).send();
		return;
	}
	if (username.isEmpty()) {
		call->fail(u"link must be an invite link or an @username."_q);
		return;
	}
	call->api.request(MTPcontacts_ResolveUsername(
		MTP_flags(0),
		MTP_string(username),
		MTPstring()
	)).done(crl::guard(call.get(), [=](
			const MTPcontacts_ResolvedPeer &result) {
		session->data().processUsers(result.data().vusers());
		session->data().processChats(result.data().vchats());
		const auto peer = session->data().peerLoaded(
			peerFromMTP(result.data().vpeer()));
		if (!peer) {
			call->fail(u"Could not resolve the chat."_q);
			return;
		} else if (!peer->asChannel()) {
			call->fail(u"Only public groups and channels can be joined by"
				" an @username."_q);
			return;
		} else if (peer->asChannel()->amIn()) {
			call->fail(u"Already a member of chat %1."_q.arg(peer->id.value));
			return;
		}
		const auto channel = peer->asChannel();
		auto card = Card{ .kind = CardKind::Change };
		card.text = tr::lng_agent_card_title_join_chat(tr::now);
		card.detail = u"join_chat"_q;
		card.rows.push_back({
			.label = tr::lng_agent_card_row_chat(tr::now),
			.value = PeerTitle(peer),
		});
		if (!peer->about().isEmpty()) {
			card.rows.push_back({
				.label = tr::lng_agent_card_row_description(tr::now),
				.value = peer->about(),
			});
		}
		call->confirm(std::move(card), [=](Card decided) {
			call->api.request(MTPchannels_JoinChannel(
				channel->inputChannel()
			)).done([=](const MTPmessages_ChatInviteJoinResult &result) {
				if (result.type()
					== mtpc_messages_chatInviteJoinResultOk) {
					session->api().applyUpdates(
						result.c_messages_chatInviteJoinResultOk().vupdates());
				}
				auto chat = QJsonObject();
				chat.insert("id", QJsonValue(qint64(channel->id.value)));
				chat.insert("title", channel->name());
				auto json = QJsonObject();
				json.insert("chat", chat);
				json.insert("joined", QJsonValue(true));
				call->done(std::move(json));
			}).fail([=](const MTP::Error &error) {
				call->fail(MtpErrorText(error));
			}).send();
		});
	})).fail(crl::guard(call.get(), [=](const MTP::Error &error) {
		call->fail(MtpErrorText(error));
	})).send();
}

void RunLeaveChat(not_null<PendingCall*> call) {
	const auto session = call->session;
	const auto peer = call->peer;
	auto card = Card{ .kind = CardKind::Change };
	card.text = tr::lng_agent_card_title_leave_chat(tr::now);
	card.detail = u"leave_chat"_q;
	card.rows.push_back({
		.label = tr::lng_agent_card_row_chat(tr::now),
		.value = PeerTitle(peer),
	});
	call->confirm(std::move(card), [=](Card decided) {
		const auto finish = [=] {
			auto final = decided;
			final.status = CardStatus::Done;
			call->finishCard(std::move(final));
		};
		if (const auto channel = peer->asChannel()) {
			session->api().leaveChannel(channel);
			finish();
		} else if (const auto chat = peer->asChat()) {
			call->api.request(MTPmessages_DeleteChatUser(
				MTP_flags(0),
				chat->inputChat(),
				session->user()->inputUser()
			)).done([=](const MTPUpdates &result) {
				session->api().applyUpdates(result);
				finish();
			}).fail([=](const MTP::Error &error) {
				call->fail(MtpErrorText(error));
			}).send();
		} else {
			Unexpected("Peer in RunLeaveChat.");
		}
	});
}

} // namespace

std::vector<ToolEntry> Entries() {
	auto result = std::vector<ToolEntry>();
	const auto push = [&](ToolEntry entry) {
		result.push_back(std::move(entry));
	};
	const auto forPeer = [](not_null<PeerData*> peer) {
		return peer->isChat() || peer->isChannel();
	};
	const auto forPeerError = u"This tool works only in groups and channels."_q;
	auto listProperties = QJsonObject();
	listProperties.insert("revoked", BoolProperty(
		u"True to list revoked links instead of active ones."_q));
	push({
		.name = u"list_invite_links"_q,
		.description = u"List the invite links of this group or channel"
			" that belong to you."_q,
		.generalDescription = u"List the invite links of the chat that"
			" belong to you."_q,
		.inputSchema = ToolSchema(std::move(listProperties), QJsonArray()),
		.policy = ToolPolicy{ .risk = Risk::Read },
		.chatParam = ChatParam::One,
		.group = u"admin"_q,
		.untrusted = false,
		.forPeer = forPeer,
		.forPeerError = forPeerError,
		.run = RunListInviteLinks,
	});
	auto createProperties = QJsonObject();
	createProperties.insert("name", StringProperty(
		u"Name of the link, up to 32 characters."_q));
	createProperties.insert("expire_at", StringProperty(
		u"Local date and time as YYYY-MM-DD HH:MM when the link"
		" expires."_q));
	createProperties.insert("usage_limit", IntProperty(1, kMaxUsageLimit,
		u"Limit of uses of the link."_q));
	createProperties.insert("request_approval", BoolProperty(
		u"True to require admin approval for join requests."_q));
	push({
		.name = u"create_invite_link"_q,
		.description = u"Create a new invite link for this group or channel."
			" The user confirms it on a card."_q,
		.generalDescription = u"Create a new invite link for the chat. The"
			" user confirms it on a card."_q,
		.inputSchema = ToolSchema(std::move(createProperties), QJsonArray()),
		.policy = ToolPolicy{ .risk = Risk::Visible, .outbound = true },
		.chatParam = ChatParam::One,
		.group = u"admin"_q,
		.forPeer = forPeer,
		.forPeerError = forPeerError,
		.run = RunCreateInviteLink,
	});
	auto editProperties = QJsonObject();
	editProperties.insert("link", StringProperty(
		u"The link to edit, from list_invite_links."_q));
	editProperties.insert("name", StringProperty(
		u"New name of the link, up to 32 characters."_q));
	editProperties.insert("expire_at", StringProperty(
		u"New local date and time as YYYY-MM-DD HH:MM when the link"
		" expires."_q));
	editProperties.insert("usage_limit", IntProperty(1, kMaxUsageLimit,
		u"New limit of uses of the link."_q));
	editProperties.insert("request_approval", BoolProperty(
		u"True to require admin approval for join requests."_q));
	push({
		.name = u"edit_invite_link"_q,
		.description = u"Edit one of your invite links in this group or"
			" channel. The user confirms it on a card."_q,
		.generalDescription = u"Edit one of your invite links in the chat."
			" The user confirms it on a card."_q,
		.inputSchema = ToolSchema(
			std::move(editProperties),
			QJsonArray{ QJsonValue(u"link"_q) }),
		.policy = ToolPolicy{ .risk = Risk::Visible, .outbound = true },
		.chatParam = ChatParam::One,
		.group = u"admin"_q,
		.forPeer = forPeer,
		.forPeerError = forPeerError,
		.run = RunEditInviteLink,
	});
	auto revokeProperties = QJsonObject();
	auto linksArray = QJsonObject();
	linksArray.insert("type", QJsonValue(u"array"_q));
	linksArray.insert("description", QJsonValue(
		u"The links to revoke, from list_invite_links."_q));
	linksArray.insert("items", StringProperty(u"An invite link string."_q));
	linksArray.insert("minItems", QJsonValue(1));
	linksArray.insert("maxItems", QJsonValue(kMaxInviteLinks));
	revokeProperties.insert("links", linksArray);
	push({
		.name = u"revoke_invite_links"_q,
		.description = u"Revoke your invite links in this group or channel."
			" The user confirms it on a card and chooses the links."_q,
		.generalDescription = u"Revoke your invite links in the chat. The"
			" user confirms it on a card and chooses the links."_q,
		.inputSchema = ToolSchema(
			std::move(revokeProperties),
			QJsonArray{ QJsonValue(u"links"_q) }),
		.policy = ToolPolicy{ .risk = Risk::Visible },
		.chatParam = ChatParam::One,
		.group = u"admin"_q,
		.forPeer = forPeer,
		.forPeerError = forPeerError,
		.run = RunRevokeInviteLinks,
	});
	auto chatInfoProperties = QJsonObject();
	chatInfoProperties.insert("title", StringProperty(
		u"New title of the chat, 1 to 128 characters."_q));
	chatInfoProperties.insert("description", StringProperty(
		u"New description of the chat, up to 255 characters."_q));
	push({
		.name = u"edit_chat_info"_q,
		.description = u"Change the title or description of a group or"
			" channel. For the user's own name or bio use update_profile."
			" The user confirms it on a card."_q,
		.generalDescription = u"Change the title and description of the chat."
			" The user confirms it on a card."_q,
		.inputSchema = ToolSchema(
			std::move(chatInfoProperties),
			QJsonArray()),
		.policy = ToolPolicy{ .risk = Risk::Visible, .outbound = true },
		.chatParam = ChatParam::One,
		.group = u"admin"_q,
		.forPeer = forPeer,
		.forPeerError = forPeerError,
		.run = RunEditChatInfo,
	});
	auto photoProperties = QJsonObject();
	photoProperties.insert("attachment", StringProperty(
		u"Attachment id of the image, like a1."_q));
	photoProperties.insert("file", StringProperty(
		u"File name of an image in the agent folder."_q));
	photoProperties.insert("remove", BoolProperty(
		u"True to remove the current chat photo."_q));
	push({
		.name = u"set_chat_photo"_q,
		.description = u"Set or remove the photo of this group or channel."
			" The user confirms it on a card."_q,
		.generalDescription = u"Set or remove the photo of the chat. The"
			" user confirms it on a card."_q,
		.inputSchema = ToolSchema(std::move(photoProperties), QJsonArray()),
		.policy = ToolPolicy{ .risk = Risk::Visible, .outbound = true },
		.chatParam = ChatParam::One,
		.group = u"admin"_q,
		.forPeer = forPeer,
		.forPeerError = forPeerError,
		.run = RunSetChatPhoto,
	});
	auto permissionsProperties = QJsonObject();
	auto deniedArray = QJsonObject();
	deniedArray.insert("type", QJsonValue(u"array"_q));
	deniedArray.insert("description", QJsonValue(
		u"What members may not do: restriction names from get_peer_info."
		" An empty array allows everything."_q));
	deniedArray.insert("items", StringProperty(
		u"A restriction name."_q));
	permissionsProperties.insert("denied", deniedArray);
	permissionsProperties.insert("slow_mode_seconds", IntProperty(0, 3600,
		u"Slow mode delay in seconds: 0, 10, 30, 60, 300, 900 or 3600,"
		" supergroups only."_q));
	push({
		.name = u"set_chat_permissions"_q,
		.description = u"Change what members may do by default in this group"
			" and the slow mode delay. The user confirms it on a card."_q,
		.generalDescription = u"Change what members may do by default in the"
			" chat and the slow mode delay. The user confirms it on a card."_q,
		.inputSchema = ToolSchema(
			std::move(permissionsProperties),
			QJsonArray()),
		.policy = ToolPolicy{ .risk = Risk::Visible },
		.chatParam = ChatParam::One,
		.group = u"admin"_q,
		.forPeer = forPeer,
		.forPeerError = forPeerError,
		.run = RunSetChatPermissions,
	});
	auto createChatProperties = QJsonObject();
	createChatProperties.insert("type", StringProperty(
		u"\"group\" or \"channel\"."_q));
	createChatProperties.insert("title", StringProperty(
		u"Title of the new chat, 1 to 128 characters."_q));
	createChatProperties.insert("description", StringProperty(
		u"Description of the new chat, up to 255 characters."_q));
	createChatProperties.insert("users", UsersProperty(50,
		u"Users to invite right away."_q));
	push({
		.name = u"create_chat"_q,
		.description = u"Create a new group or channel with the given title"
			" and description and invite the users. The user confirms it on"
			" a card."_q,
		.inputSchema = ToolSchema(
			std::move(createChatProperties),
			QJsonArray{
				QJsonValue(u"type"_q),
				QJsonValue(u"title"_q),
			}),
		.policy = ToolPolicy{
			.risk = Risk::Visible,
			.chat = false,
			.outbound = true,
		},
		.group = u"admin"_q,
		.run = RunCreateChat,
	});
	auto joinProperties = QJsonObject();
	joinProperties.insert("link", StringProperty(
		u"An invite link (t.me/+hash, t.me/joinchat/hash) or a public"
		" @username / t.me/ link to join."_q));
	push({
		.name = u"join_chat"_q,
		.description = u"Join a group or channel by an invite link or public"
			" @username. The user confirms it on a card."_q,
		.inputSchema = ToolSchema(
			std::move(joinProperties),
			QJsonArray{ QJsonValue(u"link"_q) }),
		.policy = ToolPolicy{ .risk = Risk::Visible, .chat = false },
		.group = u"admin"_q,
		.run = RunJoinChat,
	});
	push({
		.name = u"leave_chat"_q,
		.description = u"Leave this group or channel. The user confirms it on"
			" a card."_q,
		.generalDescription = u"Leave the chat. The user confirms it on a"
			" card."_q,
		.inputSchema = ToolSchema(QJsonObject(), QJsonArray()),
		.policy = ToolPolicy{ .risk = Risk::Visible },
		.chatParam = ChatParam::One,
		.group = u"admin"_q,
		.forPeer = forPeer,
		.forPeerError = forPeerError,
		.run = RunLeaveChat,
	});
	return result;
}

} // namespace Agent::AdminChatTools
