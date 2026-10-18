/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

#include "agent/core/agent_scope.h"
#include "info/info_content_widget.h"

class PeerData;

namespace Agent {
class Manager;
class Session;
} // namespace Agent

namespace Agent::Ui {

class PanelMemento final : public Info::ContentMemento {
public:
	explicit PanelMemento(not_null<PeerData*> peer);

	object_ptr<Info::ContentWidget> createWidget(
		QWidget *parent,
		not_null<Info::Controller*> controller,
		const QRect &geometry) override;

	[[nodiscard]] Info::Section section() const override;

	void setSessionId(const QString &id);
	[[nodiscard]] const QString &sessionId() const;
	void setListOpen(bool open);
	[[nodiscard]] bool listOpen() const;

private:
	QString _sessionId;
	bool _listOpen = false;

};

class PanelWidget final : public Info::ContentWidget {
public:
	PanelWidget(
		QWidget *parent,
		not_null<Info::Controller*> controller,
		not_null<PeerData*> peer);

	[[nodiscard]] not_null<PeerData*> peer() const;

	bool showInternal(
		not_null<Info::ContentMemento*> memento) override;

	void setInternalState(
		const QRect &geometry,
		not_null<Info::ContentMemento*> memento);

	rpl::producer<QString> title() override;

private:
	class Inner;
	void saveState(not_null<Info::ContentMemento*> memento);
	void restoreState(not_null<Info::ContentMemento*> memento);

	std::shared_ptr<Info::ContentMemento> doCreateMemento() override;

	const not_null<PeerData*> _peer;
	Inner *_inner = nullptr;

};

} // namespace Agent::Ui
