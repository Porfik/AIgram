/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "agent/core/tests/agent_tests.h"

#include "agent/core/agent_prompts.h"

namespace Agent::Tests {
namespace {

[[nodiscard]] QString CountCaseInsensitive(
		const QString &text,
		const QString &needle) {
	auto result = 0;
	auto position = qsizetype(0);
	while (true) {
		position = text.indexOf(needle, position, Qt::CaseInsensitive);
		if (position < 0) {
			break;
		}
		++result;
		position += needle.size();
	}
	return QString::number(result);
}

} // namespace

std::vector<Check> PromptsChecks() {
	return {
		{ u"prompts_scopes_differ"_q, [] {
			const auto brief = ChatBrief{
				.peer = 123,
				.title = u"My chat"_q,
				.kind = u"supergroup"_q,
				.username = u"mychat"_q,
				.members = 42,
				.role = u"admin"_q,
				.unread = 5,
				.canWrite = true,
			};
			const auto account = SystemPrompt(PromptContext{
				.scope = Scope::Account(),
				.account = u"Test"_q,
			});
			const auto chat = SystemPrompt(PromptContext{
				.scope = Scope::Chat(123),
				.account = u"Test"_q,
				.chat = brief,
			});
			if (account == chat) {
				return u"prompts must differ by scope"_q;
			} else if (!account.contains(u"untrusted_data"_q)
				|| !chat.contains(u"untrusted_data"_q)) {
				return u"prompts must mention untrusted_data"_q;
			} else if (!chat.contains(u"Chat: \"My chat\""_q)) {
				return u"chat prompt misses the brief"_q;
			}
			return QString();
		} },
		{ u"prompts_empty_sections_left_out"_q, [] {
			const auto empty = SystemPrompt(PromptContext{
				.scope = Scope::Account(),
			});
			if (empty.contains(u"The user's general instructions:"_q)) {
				return u"instructions label without instructions"_q;
			} else if (empty.contains(u"Memory:"_q)) {
				return u"memory label without memory"_q;
			}
			const auto filled = SystemPrompt(PromptContext{
				.scope = Scope::Account(),
				.instructions = u"Be brief."_q,
				.memory = u"Likes tea."_q,
			});
			if (!filled.contains(
					u"The user's general instructions:\nBe brief."_q)) {
				return u"instructions missing"_q;
			} else if (!filled.contains(u"Memory:\nLikes tea."_q)) {
				return u"memory missing"_q;
			}
			return QString();
		} },
		{ u"prompts_memory_paragraph_chat"_q, [] {
			const auto prompt = SystemPrompt(PromptContext{
				.scope = Scope::Chat(7),
			});
			if (!prompt.contains(u"memory for this chat"_q)) {
				return u"chat prompt misses its memory: %1"_q.arg(prompt);
			} else if (!prompt.contains(u"memory_add"_q)
				|| !prompt.contains(u"memory_update"_q)
				|| !prompt.contains(u"memory_remove"_q)) {
				return u"chat prompt misses the memory tools"_q;
			} else if (prompt.contains(u"read_chat_memory"_q)) {
				return u"chat prompt must not mention read_chat_memory"_q;
			}
			return QString();
		} },
		{ u"prompts_memory_paragraph_account"_q, [] {
			const auto prompt = SystemPrompt(PromptContext{
				.scope = Scope::Account(),
			});
			if (!prompt.contains(u"your own memory"_q)) {
				return u"account prompt misses its memory: %1"_q.arg(prompt);
			} else if (!prompt.contains(u"read_chat_memory"_q)) {
				return u"account prompt misses read_chat_memory"_q;
			}
			return QString();
		} },
		{ u"prompts_media_paragraph"_q, [] {
			for (const auto scope : { Scope::Chat(7), Scope::Account() }) {
				const auto prompt = SystemPrompt(PromptContext{
					.scope = scope,
				});
				for (const auto tool : {
					u"view_media"_q,
					u"listen_audio"_q,
					u"transcribe_voice"_q,
					u"read_file"_q,
					u"[Attachment aN"_q,
				}) {
					if (!prompt.contains(tool)) {
						return u"prompt misses %1"_q.arg(tool);
					}
				}
			}
			return QString();
		} },
		{ u"prompts_memory_block_after_paragraph"_q, [] {
			const auto prompt = SystemPrompt(PromptContext{
				.scope = Scope::Chat(7),
				.memory = u"- [m1] Likes tea."_q,
			});
			const auto position = prompt.indexOf(u"Memory:\n"_q);
			if (position < 0) {
				return u"memory block missing"_q;
			}
			const auto paragraph = prompt.indexOf(u"memory for this chat"_q);
			if (paragraph < 0 || position < paragraph) {
				return u"the memory block must follow the memory paragraph"_q;
			} else if (!prompt.contains(
					u"Memory:\n- [m1] Likes tea."_q)) {
				return u"the memory text is mangled: %1"_q.arg(prompt);
			}
			return QString();
		} },
		{ u"prompts_deterministic"_q, [] {
			const auto context = PromptContext{
				.scope = Scope::Chat(7),
				.account = u"Test"_q,
				.instructions = u"Be brief."_q,
				.memory = u"Likes tea."_q,
				.chat = ChatBrief{ .peer = 7, .title = u"Chat"_q },
			};
			return (SystemPrompt(context) == SystemPrompt(context))
				? QString()
				: u"same input gave different prompts"_q;
		} },
		{ u"prompts_format_chat_brief"_q, [] {
			const auto brief = ChatBrief{
				.peer = 123,
				.title = u"My\nchat"_q,
				.kind = u"supergroup"_q,
				.username = u"mychat"_q,
				.members = 42,
				.role = u"admin"_q,
				.unread = 5,
				.canWrite = true,
			};
			const auto text = FormatChatBrief(brief);
			const auto expected = u"Chat: \"My chat\" (supergroup, "
				u"@mychat, id 123)\nYour role: admin\n"
				u"You can write messages here: yes"_q;
			if (text != expected) {
				return u"full brief: %1"_q.arg(text);
			}
			const auto counters = FormatChatCounters(brief);
			if (counters != u"Members: 42. Unread messages: 5."_q) {
				return u"counters: %1"_q.arg(counters);
			} else if (!FormatChatCounters(ChatBrief()).isEmpty()) {
				return u"empty counters are not empty"_q;
			} else if (FormatChatCounters(ChatBrief{ .unread = 2 })
					!= u"Unread messages: 2."_q) {
				return u"unread only counters"_q;
			}
			const auto minimal = FormatChatBrief(ChatBrief{
				.title = u"Empty"_q,
			});
			const auto minimalExpected = u"Chat: \"Empty\"\n"
				u"You can write messages here: no"_q;
			if (minimal != minimalExpected) {
				return u"minimal brief: %1"_q.arg(minimal);
			}
			return QString();
		} },
		{ u"prompts_unwrap_untrusted"_q, [] {
			const auto content = u"{\"text\":\"a <untrusted_data> b\"}"_q;
			const auto shown = UnwrapUntrusted(
				u"x "_q + WrapUntrusted(content) + u" y"_q);
			const auto expected = u"x {\"text\":\"a <untrusted.data> b\"} y"_q;
			if (shown != expected) {
				return u"unexpected unwrap: %1"_q.arg(shown);
			}
			return QString();
		} },
		{ u"prompts_wrap_untrusted"_q, [] {
			const auto content = u"Try <UNTRUSTED_DATA> and "
				u"</Untrusted_Data> endings"_q;
			const auto wrapped = WrapUntrusted(content);
			if (!wrapped.startsWith(u"<untrusted_data>"_q)) {
				return u"missing opening tag: %1"_q.arg(wrapped);
			} else if (!wrapped.endsWith(u"</untrusted_data>"_q)) {
				return u"missing closing tag: %1"_q.arg(wrapped);
			}
			const auto count = CountCaseInsensitive(
				wrapped,
				u"untrusted_data"_q);
			if (count != u"2"_q) {
				return u"tags left inside: %1 occurrences"_q.arg(count);
			} else if (!wrapped.contains(u"untrusted.data"_q)) {
				return u"inner tags not defused"_q;
			}
			return QString();
		} },
		{ u"prompts_hint_text"_q, [] {
			if (!HintText(QString()).isEmpty()) {
				return u"empty hint is not empty"_q;
			} else if (!HintText(u"   \n"_q).isEmpty()) {
				return u"blank hint is not empty"_q;
			}
			const auto hint = HintText(u"line one\nline two"_q);
			if (hint != u"[Context] line one line two"_q) {
				return u"hint: %1"_q.arg(hint);
			}
			return QString();
		} },
		{ u"prompts_task_prompt"_q, [] {
			const auto context = TaskPromptContext{
				.scope = Scope::Account(),
				.account = u"Test"_q,
				.instructions = u"Be brief."_q,
				.title = u"Vacation"_q,
				.task = u"Reply that I am away."_q,
				.frame = u"Without asking you may use: send_message."_q,
			};
			const auto prompt = TaskSystemPrompt(context);
			if (!prompt.contains(u"background task \"Vacation\""_q)
				|| !prompt.contains(u"Reply that I am away."_q)
				|| !prompt.contains(u"you may use: send_message"_q)
				|| !prompt.contains(u"Be brief."_q)) {
				return u"task prompt misses parts: "_q + prompt;
			} else if (prompt.contains(u"memory"_q, Qt::CaseInsensitive)) {
				return u"task prompt must have no memory paragraph"_q;
			} else if (!prompt.contains(u"untrusted_data"_q)
				|| !prompt.contains(u"view_media"_q)) {
				return u"task prompt misses shared paragraphs"_q;
			} else if (TaskSystemPrompt(context) != prompt) {
				return u"task prompt is not stable"_q;
			}
			auto chat = context;
			chat.scope = Scope::Chat(123);
			chat.chat = ChatBrief{ .peer = 123, .title = u"Team"_q };
			const auto chatPrompt = TaskSystemPrompt(chat);
			if (!chatPrompt.contains(u"Chat: \"Team\""_q)
				|| chatPrompt.contains(u"general agent"_q)) {
				return u"chat task prompt: "_q + chatPrompt;
			}
			return QString();
		} },
	};

}

} // namespace Agent::Tests
