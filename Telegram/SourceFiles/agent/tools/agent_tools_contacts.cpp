/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "agent/tools/agent_tool_table.h"

#include "api/api_blocked_peers.h"
#include "apiwrap.h"
#include "data/data_session.h"
#include "data/data_user.h"
#include "history/history.h"
#include "lang/lang_keys.h"
#include "main/main_session.h"

#include <QtCore/QJsonArray>
#include <QtCore/QTimer>

#include <limits>

namespace Agent {
namespace {

constexpr auto kContactsLoadTimeout = crl::time(20 * 1000);

[[nodiscard]] QString UserFullName(const QString &first, const QString &last) {
	return last.isEmpty() ? first : (first + ' ' + last);
}

[[nodiscard]] QJsonObject UserJson(not_null<UserData*> user) {
	auto result = QJsonObject();
	result.insert("id", QJsonValue(qint64(user->id.value)));
	result.insert("name", PeerTitle(user));
	const auto username = user->username();
	if (!username.isEmpty()) {
		result.insert("username", username);
	}
	const auto phone = user->phone();
	if (!phone.isEmpty()) {
		result.insert("phone", phone);
	}
	const auto mutual = (user->flags() & UserDataFlag::MutualContact)
		!= UserDataFlag(0);
	result.insert("mutual", QJsonValue(mutual));
	return result;
}

[[nodiscard]] std::vector<not_null<UserData*>> MatchedContacts(
		not_null<PendingCall*> call,
		const QString &query) {
	auto result = std::vector<not_null<UserData*>>();
	for (const auto &row : call->session->data().contactsList()->all()) {
		const auto history = row->history();
		const auto user = history ? history->peer->asUser() : nullptr;
		if (!user
			|| !call->gate->visible(user->id.value)
			|| !call->gate->inFrame(user->id.value)) {
			continue;
		}
		if (!query.isEmpty()
			&& !user->name().toLower().contains(query)
			&& !user->username().toLower().contains(query)
			&& !user->phone().toLower().contains(query)) {
			continue;
		}
		result.push_back(user);
	}
	return result;
}

void DeliverContacts(
		not_null<PendingCall*> call,
		std::vector<not_null<UserData*>> all,
		int limit,
		int offset) {
	const auto total = int(all.size());
	const auto from = std::clamp(offset, 0, total);
	const auto till = std::min(from + limit, total);
	auto contacts = QJsonArray();
	for (auto i = from; i != till; ++i) {
		contacts.push_back(UserJson(all[i]));
	}
	auto json = QJsonObject();
	json.insert("contacts", contacts);
	json.insert("total", QJsonValue(total));
	json.insert("has_more", QJsonValue(till < total));
	call->done(std::move(json));
}

void RunListContacts(not_null<PendingCall*> call) {
	const auto session = call->session;
	const auto query = call->input.value("query")
		.toString()
		.trimmed()
		.toLower();
	auto error = QString();
	const auto limitValue = call->input.value("limit");
	if (!limitValue.isUndefined() && !limitValue.isNull()
		&& !limitValue.isDouble()) {
		call->fail(u"limit must be a number."_q);
		return;
	}
	const auto limit = limitValue.isDouble()
		? std::clamp(int(limitValue.toDouble()), 1, 200)
		: 100;
	const auto offsetValue = call->input.value("offset");
	if (!offsetValue.isUndefined() && !offsetValue.isNull()
		&& !offsetValue.isDouble()) {
		call->fail(u"offset must be a number."_q);
		return;
	}
	const auto offset = offsetValue.isDouble()
		? std::max(int(offsetValue.toDouble()), 0)
		: 0;
	if (!session->data().contactsLoaded().current()) {
		session->api().requestContacts();
		session->data().contactsLoaded().value(
		) | rpl::filter([](bool loaded) {
			return loaded;
		}) | rpl::take(1) | rpl::on_next([=] {
			DeliverContacts(call, MatchedContacts(call, query), limit, offset);
		}, call->lifetime);
		QTimer::singleShot(
			kContactsLoadTimeout,
			crl::guard(call.get(), [=] {
				call->fail(
					u"Loading contacts timed out, try again later."_q);
			}));
		return;
	}
	DeliverContacts(call, MatchedContacts(call, query), limit, offset);
}

void RunListBlocked(not_null<PendingCall*> call) {
	const auto session = call->session;
	auto error = QString();
	const auto limitValue = call->input.value("limit");
	if (!limitValue.isUndefined() && !limitValue.isNull()
		&& !limitValue.isDouble()) {
		call->fail(u"limit must be a number."_q);
		return;
	}
	const auto limit = limitValue.isDouble()
		? std::clamp(int(limitValue.toDouble()), 1, 200)
		: 100;
	const auto offsetValue = call->input.value("offset");
	if (!offsetValue.isUndefined() && !offsetValue.isNull()
		&& !offsetValue.isDouble()) {
		call->fail(u"offset must be a number."_q);
		return;
	}
	const auto offset = offsetValue.isDouble()
		? std::max(int(offsetValue.toDouble()), 0)
		: 0;
	session->api().blockedPeers().request(
		offset,
		crl::guard(call.get(), [=](const Api::BlockedPeers::Slice &slice) {
			auto blocked = QJsonArray();
			auto count = 0;
			for (const auto &item : slice.list) {
				if (count >= limit) {
					break;
				}
				const auto peer = session->data().peerLoaded(item.id);
				if (!peer
					|| !call->gate->visible(peer->id.value)
					|| !call->gate->inFrame(peer->id.value)) {
					continue;
				}
				auto entry = QJsonObject();
				entry.insert("id", QJsonValue(qint64(peer->id.value)));
				entry.insert("name", PeerTitle(peer));
				const auto username = peer->username();
				if (!username.isEmpty()) {
					entry.insert("username", username);
				}
				blocked.push_back(entry);
				++count;
			}
			auto json = QJsonObject();
			json.insert("blocked", blocked);
			json.insert("total", QJsonValue(slice.total));
			call->done(std::move(json));
		}));
}

void SendAddContact(
		not_null<Main::Session*> session,
		not_null<UserData*> user,
		bool sharePhone,
		const QString &first,
		const QString &last,
		Fn<void(QString error)> done) {
		using Flag = MTPcontacts_AddContact::Flag;
	session->api().request(MTPcontacts_AddContact(
		MTP_flags(sharePhone
			? Flag::f_add_phone_privacy_exception
			: Flag(0)),
		user->inputUser(),
		MTP_string(first),
		MTP_string(last),
		MTP_string(user->phone()),
		MTPTextWithEntities()
	)).done([=](const MTPUpdates &result) {
		user->setName(first, last, user->nameOrPhone, user->username());
		session->api().applyUpdates(result);
		if (const auto settings = user->barSettings()) {
			const auto barFlags = PeerBarSetting::AddContact
				| PeerBarSetting::BlockContact
				| PeerBarSetting::ReportSpam;
			user->setBarSettings(*settings & ~barFlags);
		}
		done(QString());
	}).fail([=](const MTP::Error &error) {
		done(MtpErrorText(error));
	}).send();
}

void RunAddContact(not_null<PendingCall*> call) {
	const auto session = call->session;
	const auto user = call->peer ? call->peer->asUser() : nullptr;
	if (!user) {
		call->fail(u"chat must be a user, not a group or channel."_q);
		return;
	} else if (user->isContact()) {
		call->fail(u"%1 is a contact already, use rename_contact to change"
			" the name."_q.arg(PeerTitle(user)));
		return;
	}
	const auto first = call->input.value("first_name").toString().trimmed();
	const auto last = call->input.value("last_name").toString().trimmed();
	if (first.isEmpty()) {
		call->fail(u"first_name is required."_q);
		return;
	}
	const auto share = call->input.value("share_phone").toBool(false);
	const auto phone = user->phone();
	auto card = Card{ .kind = CardKind::Change };
	card.text = tr::lng_agent_card_title_add_contact(tr::now);
	card.detail = u"add_contact"_q;
	card.rows.push_back({
		.label = tr::lng_agent_card_row_first_name(tr::now),
		.value = first,
	});
	if (!last.isEmpty()) {
		card.rows.push_back({
			.label = tr::lng_agent_card_row_last_name(tr::now),
			.value = last,
		});
	}
	if (!phone.isEmpty()) {
		card.rows.push_back({
			.label = tr::lng_agent_card_row_phone(tr::now),
			.value = phone,
		});
	}
	card.rows.push_back({
		.label = tr::lng_agent_card_row_share_phone(tr::now),
		.value = share
			? tr::lng_agent_card_value_yes(tr::now)
			: tr::lng_agent_card_value_no(tr::now),
	});
	card.targets.push_back(PeerTarget(user));
	call->confirm(std::move(card), [=](Card decided) {
		ForEachTarget(call, decided, [=](
				const CardTarget &target,
				Fn<void(QString error)> done) {
			const auto user = call->session->data().peerLoaded(
				PeerId(target.peer))->asUser();
			if (!user) {
				done(u"The user is not loaded any more."_q);
				return;
			}
			SendAddContact(session, user, share, first, last, done);
		}, [=](Card finished) {
			call->finishCard(std::move(finished));
		});
	});
}

void RunRenameContact(not_null<PendingCall*> call) {
	const auto user = call->peer ? call->peer->asUser() : nullptr;
	if (!user) {
		call->fail(u"chat must be a user, not a group or channel."_q);
		return;
	} else if (!user->isContact()) {
		call->fail(u"%1 is not a contact, use add_contact to add them."_q
			.arg(PeerTitle(user)));
		return;
	}
	const auto first = call->input.value("first_name").toString().trimmed();
	const auto last = call->input.value("last_name").toString().trimmed();
	if (first.isEmpty()) {
		call->fail(u"first_name is required."_q);
		return;
	}
	const auto oldFirst = user->firstName;
	const auto oldLast = user->lastName;
	if (oldFirst == first && oldLast == last) {
		call->fail(u"That is the name of the contact already."_q);
		return;
	}
	const auto session = call->session;
	SendAddContact(
		session,
		user,
		false,
		first,
		last,
		crl::guard(call.get(), [=](QString error) {
			if (!error.isEmpty()) {
				call->fail(error);
				return;
			}
			call->undoable([=](Fn<void(QString error)> finished) {
				SendAddContact(
					session,
					user,
					false,
					oldFirst,
					oldLast,
					finished);
			});
			auto done = QJsonArray({ QJsonObject({
				{ "chat", QJsonValue(qint64(user->id.value)) },
				{ "title", PeerTitle(user) },
			}) });
			auto json = QJsonObject();
			json.insert("done", done);
			json.insert("failed", QJsonArray());
			json.insert("name", UserFullName(first, last));
			call->done(std::move(json));
		}));
}

void RunDeleteContacts(not_null<PendingCall*> call) {
	const auto session = call->session;
	auto users = std::vector<not_null<UserData*>>();
	users.reserve(call->peers.size());
	for (const auto &peer : call->peers) {
		const auto user = peer->asUser();
		if (!user) {
			call->fail(u"%1 is not a user, contacts work only with users."_q
				.arg(PeerTitle(peer)));
			return;
		} else if (!user->isContact()) {
			call->fail(u"%1 is not a contact."_q.arg(PeerTitle(user)));
			return;
		}
		users.push_back(user);
	}
	auto card = Card{ .kind = CardKind::Change };
	card.text = tr::lng_agent_card_title_delete_contacts(tr::now);
	card.detail = u"delete_contacts"_q;
	for (const auto &user : users) {
		card.targets.push_back(PeerTarget(user));
	}
	call->confirm(std::move(card), [=](Card decided) {
		auto checked = QVector<MTPInputUser>();
		for (auto i = 0; i != int(decided.targets.size()); ++i) {
			if (decided.targets[i].checked) {
				checked.push_back(users[i]->inputUser());
			}
		}
		if (checked.isEmpty()) {
			decided.status = CardStatus::Done;
			call->finishCard(std::move(decided));
			return;
		}
		call->api.request(MTPcontacts_DeleteContacts(
			MTP_vector<MTPInputUser>(checked)
		)).done([=](const MTPUpdates &result) mutable {
			session->api().applyUpdates(result);
			decided.status = CardStatus::Done;
			call->finishCard(std::move(decided));
		}).fail([=](const MTP::Error &error) mutable {
			const auto text = MtpErrorText(error);
			for (auto &target : decided.targets) {
				if (target.checked) {
					target.error = text;
				}
			}
			decided.status = CardStatus::Failed;
			call->finishCard(std::move(decided));
		}).send();
	});
}

void RunSetBlocked(not_null<PendingCall*> call) {
	const auto blocked = call->input.value("blocked");
	if (!blocked.isBool()) {
		call->fail(u"blocked is required, true to block or false to"
			" unblock."_q);
		return;
	}
	const auto blocking = blocked.toBool();
	const auto session = call->session;
	for (const auto &peer : call->peers) {
		if (!peer->isUser()) {
			call->fail(u"%1 is not a user, contacts and blocking work only"
				" with users."_q.arg(PeerTitle(peer)));
			return;
		}
	}
	auto card = Card{ .kind = CardKind::Change };
	card.text = blocking
		? tr::lng_agent_card_title_block(tr::now)
		: tr::lng_agent_card_title_unblock(tr::now);
	card.detail = u"set_blocked"_q;
	for (const auto &peer : call->peers) {
		card.targets.push_back(PeerTarget(peer));
	}
	call->confirm(std::move(card), [=](Card decided) {
		ForEachTarget(call, decided, [=](
				const CardTarget &target,
				Fn<void(QString error)> done) {
			const auto user = session->data().peerLoaded(
				PeerId(target.peer))->asUser();
			if (!user) {
				done(u"The user is not loaded any more."_q);
				return;
			}
			const auto finish = [=](const MTPBool &) {
				user->setIsBlocked(blocking);
				done(QString());
			};
			const auto fail = [=](const MTP::Error &error) {
				done(MtpErrorText(error));
			};
			if (blocking) {
				call->api.request(MTPcontacts_Block(
					MTP_flags(0),
					user->input()
				)).done(finish).fail(fail).send();
			} else {
				call->api.request(MTPcontacts_Unblock(
					MTP_flags(0),
					user->input()
				)).done(finish).fail(fail).send();
			}
		}, [=](Card finished) {
			session->api().blockedPeers().reload();
			call->finishCard(std::move(finished));
		});
	});
}

} // namespace

namespace ContactTools {

std::vector<ToolEntry> Entries() {
	auto result = std::vector<ToolEntry>();
	const auto push = [&](ToolEntry entry) {
		result.push_back(std::move(entry));
	};
	const auto userOnly = [](not_null<PeerData*> peer) {
		return peer->isUser() && !peer->isSelf();
	};
	push(ToolEntry{
		.name = u"list_contacts"_q,
		.description = u"List the user's contacts with ids, names,"
			" usernames, phones and mutual flags. Filter by query and"
			" page with limit and offset."_q,
		.inputSchema = ToolSchema(
			[] {
				auto properties = QJsonObject();
				properties.insert("query", StringProperty(
					u"Filter by name, @username or phone, ignoring case."_q));
				properties.insert("limit", IntProperty(1, 200,
					u"How many contacts to return, default 100."_q));
				properties.insert("offset", IntProperty(0,
					std::numeric_limits<int>::max(),
					u"How many contacts to skip."_q));
				return properties;
			}(),
			QJsonArray()),
		.policy = ToolPolicy{
			.risk = Risk::Read,
			.general = true,
			.chat = false,
		},
		.group = u"contacts"_q,
		.run = RunListContacts,
	});
	push(ToolEntry{
		.name = u"list_blocked"_q,
		.description = u"List the users the user has blocked, with ids,"
			" names and usernames. Page with limit and offset."_q,
		.inputSchema = ToolSchema(
			[] {
				auto properties = QJsonObject();
				properties.insert("limit", IntProperty(1, 200,
					u"How many entries to return, default 100."_q));
				properties.insert("offset", IntProperty(0,
					std::numeric_limits<int>::max(),
					u"How many entries to skip."_q));
				return properties;
			}(),
			QJsonArray()),
		.policy = ToolPolicy{
			.risk = Risk::Read,
			.general = true,
			.chat = false,
		},
		.group = u"contacts"_q,
		.run = RunListBlocked,
	});
	push(ToolEntry{
		.name = u"add_contact"_q,
		.description = u"Add a user to the user's contacts by their"
			" name. The chat must be a private chat with a person."_q,
		.inputSchema = ToolSchema(
			[] {
				auto properties = QJsonObject();
				properties.insert("first_name", StringProperty(
					u"New first name, required."_q));
				properties.insert("last_name", StringProperty(
					u"New last name."_q));
				properties.insert("share_phone", BoolProperty(
					u"True to also let the contact see the user's phone"
						" number, default false."_q));
				return properties;
			}(),
			QJsonArray{ QJsonValue(u"first_name"_q) }),
		.policy = ToolPolicy{
			.risk = Risk::Visible,
			.general = true,
			.chat = true,
		},
		.chatParam = ChatParam::One,
		.group = u"contacts"_q,
		.forPeer = userOnly,
		.forPeerError = u"Contacts and blocking work only with users."_q,
		.run = RunAddContact,
	});
	push(ToolEntry{
		.name = u"rename_contact"_q,
		.description = u"Rename a contact in the user's address book. The"
			" chat must be a private chat with a person."_q,
		.inputSchema = ToolSchema(
			[] {
				auto properties = QJsonObject();
				properties.insert("first_name", StringProperty(
					u"New first name, required."_q));
				properties.insert("last_name", StringProperty(
					u"New last name, empty to remove it."_q));
				return properties;
			}(),
			QJsonArray{ QJsonValue(u"first_name"_q) }),
		.policy = ToolPolicy{
			.risk = Risk::Reversible,
			.general = true,
			.chat = true,
		},
		.chatParam = ChatParam::One,
		.group = u"contacts"_q,
		.action = true,
		.forPeer = userOnly,
		.forPeerError = u"Contacts and blocking work only with users."_q,
		.run = RunRenameContact,
	});
	push(ToolEntry{
		.name = u"delete_contacts"_q,
		.description = u"Remove users from the user's contacts. Chats must"
			" be users that are contacts."_q,
		.inputSchema = ToolSchema(QJsonObject(), QJsonArray()),
		.policy = ToolPolicy{
			.risk = Risk::Visible,
			.general = true,
			.chat = true,
		},
		.chatParam = ChatParam::Many,
		.group = u"contacts"_q,
		.forPeer = userOnly,
		.forPeerError = u"Contacts and blocking work only with users."_q,
		.run = RunDeleteContacts,
	});
	push(ToolEntry{
		.name = u"set_blocked"_q,
		.description = u"Block or unblock users so that they can or can't"
			" write to the user. Chats must be users."_q,
		.inputSchema = ToolSchema(
			[] {
				auto properties = QJsonObject();
				properties.insert("blocked", BoolProperty(
					u"True to block the users, false to unblock them."_q));
				return properties;
			}(),
			QJsonArray{ QJsonValue(u"blocked"_q) }),
		.policy = ToolPolicy{
			.risk = Risk::Visible,
			.general = true,
			.chat = true,
		},
		.chatParam = ChatParam::Many,
		.group = u"contacts"_q,
		.forPeer = userOnly,
		.forPeerError = u"Contacts and blocking work only with users."_q,
		.run = RunSetBlocked,
	});
	return result;
}

} // namespace Agent::ContactTools

} // namespace Agent
