/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "agent/ui/agent_profile.h"

#include "agent/agent_chat.h"
#include "agent/agent_manager.h"
#include "agent/agent_memories.h"
#include "agent/agent_settings.h"
#include "agent/ui/agent_feed_format.h"
#include "agent/tools/agent_tools.h"
#include "agent/core/agent_journal.h"
#include "agent/agent_session_store.h"
#include "agent/agent_session.h"
#include "agent/agent_tasks.h"
#include "agent/ui/agent_memory_list.h"
#include "agent/ui/agent_model_boxes.h"
#include "agent/ui/agent_section.h"
#include "agent/ui/agent_tasks_view.h"
#include "data/data_peer.h"
#include "data/data_session.h"
#include "ui/text/format_values.h"
#include "info/info_controller.h"
#include "info/info_memento.h"
#include "info/info_top_bar.h"
#include "lang/lang_keys.h"
#include "main/main_session.h"
#include "settings/settings_common.h"
#include "settings/sections/settings_agent.h"
#include "ui/controls/userpic_button.h"
#include "ui/layers/generic_box.h"
#include "ui/ui_utility.h"
#include "ui/vertical_list.h"
#include "ui/widgets/buttons.h"
#include "ui/widgets/checkbox.h"
#include "ui/widgets/fields/input_field.h"
#include "ui/widgets/labels.h"
#include "ui/wrap/slide_wrap.h"
#include "ui/wrap/vertical_layout.h"
#include "window/window_session_controller.h"
#include "styles/style_agent_general.h"
#include "styles/style_boxes.h"
#include "styles/style_info.h"
#include "styles/style_layers.h"
#include "styles/style_menu_icons.h"
#include "styles/style_settings.h"

#include <QtCore/QBuffer>

namespace Agent::Ui {
namespace {

constexpr auto kPhotoSize = 320;

[[nodiscard]] QByteArray PhotoToBytes(const QImage &image) {
	const auto scaled = image.scaled(
		kPhotoSize,
		kPhotoSize,
		Qt::KeepAspectRatioByExpanding,
		Qt::SmoothTransformation);
	auto result = QByteArray();
	auto buffer = QBuffer(&result);
	scaled.save(&buffer, "JPG", 90);
	return result;
}

[[nodiscard]] QString DefaultModelLabel() {
	const auto providers = LoadProviders();
	const auto provider = DefaultProvider(providers);
	return provider
		? ModelTitle(*provider)
		: tr::lng_agent_media_model_none(tr::now);
}

void DefaultModelChoiceBox(not_null<::Ui::GenericBox*> box) {
	box->setTitle(tr::lng_agent_default_model_title());
	const auto providers = LoadProviders();
	const auto choices = providers.list;
	const auto current = DefaultProvider(providers);
	auto index = -1;
	for (auto i = 0; i != int(choices.size()); ++i) {
		if (current && choices[i].id == current->id) {
			index = i;
		}
	}
	const auto group = std::make_shared<::Ui::RadioenumGroup<int>>(index);
	for (auto i = 0; i != int(choices.size()); ++i) {
		box->addRow(
			object_ptr<::Ui::Radioenum<int>>(
				box,
				group,
				i,
				ModelTitle(choices[i]),
				st::defaultBoxCheckbox),
			st::settingsSendTypePadding);
	}
	group->setChangedCallback([=](int index) {
		const auto id = choices[index].id;
		UpdateProviders([&](ProvidersState &state) {
			state.defaultId = id;
		});
		box->closeBox();
	});
	box->addButton(tr::lng_close(), [=] {
		box->closeBox();
	});
}

class JournalList final : public ::Ui::RpWidget {
public:
	JournalList(
		QWidget *parent,
		not_null<Window::SessionController*> controller);

protected:
	int resizeGetHeight(int newWidth) override;

private:
	void rebuild();
	[[nodiscard]] std::vector<SessionRecord> readRecords() const;
	[[nodiscard]] QString entryText(const JournalEntry &entry) const;
	void open(const JournalEntry &entry);

	const not_null<Window::SessionController*> _controller;
	const not_null<Manager*> _manager;
	object_ptr<::Ui::VerticalLayout> _content;

};

JournalList::JournalList(
	QWidget *parent,
	not_null<Window::SessionController*> controller)
: RpWidget(parent)
, _controller(controller)
, _manager(&controller->session().agent())
, _content(this) {
	_content->heightValue(
	) | rpl::on_next([=] {
		resizeToWidth(width());
	}, lifetime());
	rebuild();
}

std::vector<SessionRecord> JournalList::readRecords() const {
	auto ids = std::vector<QString>();
	for (const auto &info : _manager->listAll()) {
		ids.push_back(info.id);
	}
	for (const auto &task : _manager->tasks().list()) {
		for (const auto &run : _manager->tasks().runs(task.id)) {
			ids.push_back(run.sessionId);
		}
	}
	auto result = std::vector<SessionRecord>();
	const auto store = SessionStore(&_manager->session());
	for (const auto &id : ids) {
		if (const auto session = _manager->loaded(id)) {
			result.push_back(session->record());
		} else if (auto record = store.readSession(id)) {
			result.push_back(std::move(*record));
		}
	}
	return result;
}

QString JournalList::entryText(const JournalEntry &entry) const {
	if (entry.card) {
		auto titles = QStringList();
		for (const auto &target : entry.card->targets) {
			if (target.error.isEmpty()) {
				titles.push_back(target.title);
			}
		}
		const auto &card = *entry.card;
		const auto title = ((card.kind == CardKind::Change
					|| card.kind == CardKind::Method)
				&& !card.text.isEmpty())
			? card.text
			: CardTitleText(card.kind);
		return titles.isEmpty()
			? title
			: (title + u": "_q + titles.join(u", "_q));
	}
	if (entry.result && entry.result->error) {
		return ToolTitleText(entry.call, entry.result, nullptr);
	}
	const auto parsed = ParseActionResult(entry.result);
	return ActionMainText(
		entry.call.name,
		entry.call.input,
		parsed.chats(),
		parsed.folder,
		parsed.fact,
		parsed);
}

void JournalList::open(const JournalEntry &entry) {
	if (entry.scope.kind == ScopeKind::Account || !entry.task.isEmpty()) {
		ShowGeneralAgentSession(_controller, entry.sessionId);
	} else if (const auto peer = _manager->session().data().peerLoaded(
			PeerId(entry.scope.peer))) {
		_controller->showPeerHistory(peer);
	}
}

void JournalList::rebuild() {
	_content->clear();
	const auto entries = BuildJournal(
		readRecords(),
		ActionToolNames(),
		200);
	if (entries.empty()) {
		_content->add(
			object_ptr<::Ui::FlatLabel>(
				_content,
				tr::lng_agent_journal_empty(tr::now),
				st::agentProfileStatus),
			st::agentJournalEmptyPadding);
	}
	for (const auto &entry : entries) {
		const auto mark = QString(QChar(entry.failed ? 0x2717 : 0x2713));
		const auto button = _content->add(object_ptr<::Ui::SettingsButton>(
			_content,
			rpl::single(mark + ' ' + entryText(entry)),
			st::agentJournalButton));
		const auto date = FormatDateTime(
			QDateTime::fromSecsSinceEpoch(entry.date));
		auto about = date;
		if (entry.undone) {
			about += u" · "_q + tr::lng_agent_undone(tr::now);
		}
		about += u" · "_q + (entry.sessionTitle.isEmpty()
			? tr::lng_agent_session_untitled(tr::now)
			: entry.sessionTitle);
		const auto label = ::Ui::CreateChild<::Ui::FlatLabel>(
			button,
			about,
			st::agentJournalAbout);
		label->setAttribute(Qt::WA_TransparentForMouseEvents);
		const auto session = _manager->loaded(entry.sessionId);
		const auto id = entry.call.id;
		const auto undo = (!entry.undone && session && session->canUndo(id))
			? ::Ui::CreateChild<::Ui::LinkButton>(
				button,
				tr::lng_agent_undo(tr::now))
			: nullptr;
		button->widthValue(
		) | rpl::on_next([=](int width) {
			const auto &padding = st::agentJournalButton.padding;
			const auto right = undo ? (undo->width() + padding.right()) : 0;
			label->resizeToWidth(std::max(
				width - padding.left() - padding.right() - right,
				1));
			label->moveToLeft(padding.left(), st::agentJournalAboutTop);
			if (undo) {
				undo->moveToRight(
					padding.right(),
					(button->height() - undo->height()) / 2);
			}
		}, button->lifetime());
		if (undo) {
			const auto weak = base::make_weak(session);
			undo->setClickedCallback(crl::guard(this, [=] {
				if (const auto strong = weak.get()) {
					strong->undo({ id });
				}
				crl::on_main(this, [=] { rebuild(); });
			}));
		}
		button->setClickedCallback([=, entry = entry] {
			open(entry);
		});
	}
	::Ui::AddSkip(_content);
	::Ui::AddDividerText(_content, tr::lng_agent_journal_about());
	_content->showChildren();
	resizeToWidth(width());
}

int JournalList::resizeGetHeight(int newWidth) {
	_content->resizeToWidth(newWidth);
	_content->moveToLeft(0, 0, newWidth);
	return _content->height();
}

} // namespace

ProfileMemento::ProfileMemento(
	not_null<PeerData*> peer,
	ProfileMode mode,
	QString taskId)
: Info::ContentMemento(peer, nullptr, nullptr, PeerId())
, _mode(mode)
, _taskId(std::move(taskId)) {
}

object_ptr<Info::ContentWidget> ProfileMemento::createWidget(
		QWidget *parent,
		not_null<Info::Controller*> controller,
		const QRect &geometry) {
	auto result = object_ptr<ProfileWidget>(
		parent,
		controller,
		peer(),
		_mode,
		_taskId);
	result->setInternalState(geometry, this);
	return result;
}

Info::Section ProfileMemento::section() const {
	return Info::Section(Info::Section::Type::Agent);
}

ProfileMode ProfileMemento::mode() const {
	return _mode;
}

QString ProfileMemento::taskId() const {
	return _taskId;
}

ProfileWidget::ProfileWidget(
	QWidget *parent,
	not_null<Info::Controller*> controller,
	not_null<PeerData*> peer,
	ProfileMode mode,
	QString taskId)
: Info::ContentWidget(parent, controller)
, _peer(peer)
, _mode(mode)
, _taskId(std::move(taskId)) {
	const auto window = controller->parentController();
	const auto manager = &peer->session().agent();
	auto owned = object_ptr<::Ui::VerticalLayout>(this);
	const auto container = owned.data();
	switch (_mode) {
	case ProfileMode::Main:
		setupMain(container);
		break;
	case ProfileMode::Tasks:
	case ProfileMode::Task: {
		const auto tasks = container->add(object_ptr<TasksView>(
			container,
			window,
			manager,
			Scope::Account()));
		if (_mode == ProfileMode::Task) {
			tasks->showSingleTask(_taskId);
			tasks->closeRequests(
			) | rpl::on_next([=] {
				this->controller()->showBackFromStack();
			}, tasks->lifetime());
		} else {
			tasks->setOpenTaskHandler([=](const QString &id) {
				showMode(ProfileMode::Task, id);
			});
		}
		tasks->openRunRequests(
		) | rpl::on_next([=](const QString &id) {
			ShowGeneralAgentSession(window, id);
		}, tasks->lifetime());
	} break;
	case ProfileMode::Memory:
		container->add(object_ptr<MemoryList>(
			container,
			window,
			manager,
			PeerKey(0)));
		break;
	case ProfileMode::Journal:
		container->add(object_ptr<JournalList>(container, window));
		break;
	}
	setInnerWidget(std::move(owned));
}

void ProfileWidget::setupMain(not_null<::Ui::VerticalLayout*> container) {
	const auto window = controller()->parentController();
	const auto manager = &_peer->session().agent();
	const auto chat = &manager->chat();

	::Ui::AddSkip(container, st::agentProfileCoverSkip);
	container->add(
		object_ptr<::Ui::UserpicButton>(
			container,
			_peer,
			st::agentProfileUserpic),
		style::al_top);
	::Ui::AddSkip(container, st::agentProfileNameSkip);
	container->add(
		object_ptr<::Ui::FlatLabel>(
			container,
			chat->nameValue(),
			st::agentProfileName),
		style::al_top);
	container->add(
		object_ptr<::Ui::FlatLabel>(
			container,
			tr::lng_agent_profile_status(),
			st::agentProfileStatus),
		style::al_top);
	::Ui::AddSkip(container, st::agentProfileCoverSkip);
	::Ui::AddDivider(container);
	::Ui::AddSkip(container);

	const auto modelLabel = rpl::single(
		rpl::empty
	) | rpl::then(
		ProvidersChanged()
	) | rpl::map(DefaultModelLabel);
	Settings::AddButtonWithLabel(
		container,
		tr::lng_agent_default_model(),
		modelLabel,
		st::settingsButton,
		{ &st::agentMenuIconPass }
	)->setClickedCallback([=] {
		if (!LoadProviders().list.empty()) {
			window->show(Box(DefaultModelChoiceBox));
		}
	});

	const auto tasksLabel = rpl::single(
		rpl::empty
	) | rpl::then(
		manager->tasks().changes() | rpl::to_empty
	) | rpl::map([=] {
		return QString::number(manager->tasks().list(Scope::Account()).size());
	});
	Settings::AddButtonWithLabel(
		container,
		tr::lng_agent_tasks_tab(),
		tasksLabel,
		st::settingsButton,
		{ &st::menuIconSchedule }
	)->setClickedCallback([=] {
		showMode(ProfileMode::Tasks);
	});

	const auto memoryLabel = rpl::single(
		rpl::empty
	) | rpl::then(
		manager->memories().changes() | rpl::filter([](PeerKey owner) {
			return !owner;
		}) | rpl::to_empty
	) | rpl::map([=] {
		return QString::number(manager->memories().get(0).facts.size());
	});
	Settings::AddButtonWithLabel(
		container,
		tr::lng_agent_memory_tab(),
		memoryLabel,
		st::settingsButton,
		{ &st::menuIconFave }
	)->setClickedCallback([=] {
		showMode(ProfileMode::Memory);
	});

	Settings::AddButtonWithIcon(
		container,
		tr::lng_agent_journal(),
		st::settingsButton,
		{ &st::menuIconRestore }
	)->setClickedCallback([=] {
		showMode(ProfileMode::Journal);
	});

	Settings::AddButtonWithIcon(
		container,
		tr::lng_agent_profile_settings(),
		st::settingsButton,
		{ &st::menuIconSettings }
	)->setClickedCallback([=] {
		window->showSettings(Settings::AgentId());
	});
	::Ui::AddSkip(container);
	::Ui::AddDividerText(container, tr::lng_agent_profile_about());
}

void ProfileWidget::showMode(ProfileMode mode, QString taskId) {
	controller()->showSection(std::make_shared<Info::Memento>(
		std::vector<std::shared_ptr<Info::ContentMemento>>{
			std::make_shared<ProfileMemento>(_peer, mode, std::move(taskId)),
		}));
}

bool ProfileWidget::showInternal(not_null<Info::ContentMemento*> memento) {
	if (!controller()->validateMementoPeer(memento)) {
		return false;
	} else if (const auto my = dynamic_cast<ProfileMemento*>(memento.get())) {
		return (my->mode() == _mode) && (my->taskId() == _taskId);
	}
	return false;
}

void ProfileWidget::setInternalState(
		const QRect &geometry,
		not_null<Info::ContentMemento*> memento) {
	setGeometry(geometry);
	::Ui::SendPendingMoveResizeEvents(this);
}

rpl::producer<QString> ProfileWidget::title() {
	switch (_mode) {
	case ProfileMode::Tasks:
	case ProfileMode::Task: return tr::lng_agent_tasks_tab();
	case ProfileMode::Memory: return tr::lng_agent_memory_tab();
	case ProfileMode::Journal: return tr::lng_agent_journal();
	case ProfileMode::Main: break;
	}
	return tr::lng_agent_profile_title();
}

void ProfileWidget::setupTopBar(
		not_null<Info::TopBar*> topBar,
		Info::Wrap wrap) {
	if (_mode != ProfileMode::Main) {
		return;
	}
	const auto layer = (wrap == Info::Wrap::Layer);
	const auto edit = topBar->addButton(
		base::make_unique_q<::Ui::IconButton>(
			topBar,
			(layer ? st::infoLayerTopBarEdit : st::infoTopBarEdit)));
	edit->setAccessibleName(tr::lng_agent_profile_edit_title(tr::now));
	edit->setClickedCallback([=] {
		ShowEditAgentProfileBox(controller()->parentController());
	});
}

std::shared_ptr<Info::ContentMemento> ProfileWidget::doCreateMemento() {
	return std::make_shared<ProfileMemento>(_peer, _mode, _taskId);
}

void ShowEditAgentProfileBox(
		not_null<Window::SessionController*> controller) {
	const auto chat = &controller->session().agent().chat();
	controller->show(Box([=](not_null<::Ui::GenericBox*> box) {
		struct State {
			std::optional<QImage> image;
		};
		const auto state = box->lifetime().make_state<State>();
		const auto providers = LoadProviders();
		box->setTitle(tr::lng_agent_profile_edit_title());

		const auto userpic = box->addRow(
			object_ptr<::Ui::UserpicButton>(
				box,
				&controller->window(),
				::Ui::UserpicButton::Role::ChoosePhoto,
				st::defaultUserpicButton),
			style::al_top);
		userpic->showCustom(chat->photo());
		userpic->chosenImages(
		) | rpl::on_next([=](::Ui::UserpicButton::ChosenImage &&chosen) {
			state->image = chosen.image;
			userpic->showCustom(std::move(chosen.image));
		}, userpic->lifetime());

		const auto reset = box->addRow(
			object_ptr<::Ui::SlideWrap<::Ui::LinkButton>>(
				box,
				object_ptr<::Ui::LinkButton>(
					box,
					tr::lng_agent_profile_photo_reset(tr::now))),
			st::agentProfileResetPadding,
			style::al_top);
		reset->toggle(!providers.agentPhoto.isEmpty(), anim::type::instant);
		reset->entity()->setClickedCallback([=] {
			state->image = QImage();
			userpic->showCustom(DefaultAgentPhoto(kPhotoSize));
			reset->toggle(false, anim::type::normal);
		});
		userpic->chosenImages(
		) | rpl::on_next([=] {
			reset->toggle(true, anim::type::normal);
		}, reset->lifetime());

		const auto name = box->addRow(
			object_ptr<::Ui::InputField>(
				box,
				st::defaultInputField,
				tr::lng_agent_profile_name(),
				AgentName(providers.agentName)),
			st::boxRowPadding);
		name->setMaxLength(64);
		box->setFocusCallback([=] {
			name->setFocusFast();
		});
		const auto save = [=] {
			const auto text = name->getLastText().trimmed();
			UpdateProviders([&](ProvidersState &providers) {
				providers.agentName = (text == AgentName(QString()))
					? QString()
					: text;
				if (state->image) {
					providers.agentPhoto = state->image->isNull()
						? QByteArray()
						: PhotoToBytes(*state->image);
				}
			});
			box->closeBox();
		};
		name->submits() | rpl::on_next(save, name->lifetime());
		box->addButton(tr::lng_settings_save(), save);
		box->addButton(tr::lng_cancel(), [=] {
			box->closeBox();
		});
	}));
}

} // namespace Agent::Ui
