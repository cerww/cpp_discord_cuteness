#pragma once
#include "../include/partial_message.h"
#include "../include/guild_text_message.h"
#include "../include/guild.h"
#include "../include/guild_channel.h"
#include "../include/voice_channel.h"
#include "../include/text_channel.h"
#include <variant>
#include "parsing_stuff.h"
#include "parser_adapters.h"

struct shard;

namespace shiny {

using namespace parse_literals;

enum class argument_type {
	string,
	number,
	guild_member,
	role,
	emoji//custom
};

struct arg_thing {
	argument_type type;
	std::variant<snowflake, std::string_view, int> data;
};

struct word {};

struct rest {};

template<typename arg_type>
struct parser {
	parser() = delete;
};

//parser<word>
//parser<std::string>
//parser<std::string_view>
//parser<guild_member>
//parser<guild_role>
//parser<partial_channel>
//parser<voice_channel>
//parser<text_channel>
//parser<int>
//parser<std::array<T,N>> where T is parsable_arg
//parser<std::vector<T>> where T is parsable
//parser<emoji> (custom emoji only)
//parser<rest>
//parser<std::variant<T...> where T... is all parsable_arg

template<typename T>
concept parsable_arg = std::is_constructible_v<parser<T>, const guild_text_message&>;

struct parser_base_helper {
	explicit parser_base_helper(const guild_text_message& t_msg) :
		msg(t_msg) { }

	const guild_text_message& msg;
};

template<>
struct parser<guild_member> {

	explicit parser(const guild_text_message& t_msg) :
		msg(t_msg) { }

	parse_result<const guild_member*> operator()(std::string_view s) const {
		return try_parse_multiple(
			s,
			transform_parser(multi_parser("<@"_p, uint64_parser(), '>'_p), return_nth<1>()),
			transform_parser(multi_parser("<@!"_p, uint64_parser(), '>'_p), return_nth<1>())
		).and_then([&](const uint64_t id_as_int)->std::optional<const guild_member*> {
			auto id = snowflake(id_as_int);
			const auto it = ranges::find(msg.mentions(), id, &guild_member::id);
			if (it == msg.mentions().end()) {
				return std::nullopt;
			} else {
				return &*it;
			}
		});
	}

	const guild_text_message& msg;

};

template<>
struct parser<emoji> {
	parser(const guild_text_message& t_msg):
		msg(t_msg) { }

	parse_result<const emoji*> operator()(std::string_view s) const {
		return try_parse_multiple(s,
								  multi_parser("<:"_p, parse_until_char(':'), ':'_p, uint64_parser(), '>'_p),
								  multi_parser("<a:"_p, parse_until_char(':'), ':'_p, uint64_parser(), '>'_p)
		).apply_transform([&](auto, auto name, auto, auto id, auto) ->std::optional<const emoji*> {
			const auto& guild = msg.guild();
			auto emoji_it = ranges::find_if(guild.emojis(), [&](const emoji& emoji) {
				return emoji.id() == snowflake(id) && emoji.name() == name;
			});

			if (emoji_it == guild.emojis().end()) {
				return std::nullopt;
			}
			return &*emoji_it;
		}).and_then(std::identity());//lol
	}


	const guild_text_message& msg;
};

//like parser<emoji> but supports non-custom emojis
template<>
struct parser<partial_emoji> {
	parser(const guild_text_message& t_msg) :
		msg(t_msg) { }

	parse_result<partial_emoji> operator()(std::string_view s) const {
		auto custom_emoji_parse_result = parser<emoji>(msg)(s);
		if (custom_emoji_parse_result) {
			return custom_emoji_parse_result.transform([](const emoji* emo) {
				return partial_emoji(*emo);
			});
		} else {
			return parse_consecutive(s, ':'_p, parse_until_char(':', ' ', '\n', '\t', '-'), ':'_p)//this might not work?
				   .transform(return_nth<1>())
				   .transform([](std::string_view emoji_name) {
					   return partial_emoji(std::string(emoji_name));
				   });
		}
	}

	const guild_text_message& msg;
};

template<>
struct parser<word> {
	explicit parser(const guild_text_message&) {}

	parse_result<std::string_view> operator()(std::string_view s) const {
		return try_parse_multiple(s, quote_string_parser(), parse_until_char(' '));
	}
};

template<>
struct parser<std::string> {
	explicit parser(const guild_text_message&) {}

	parse_result<std::string> operator()(std::string_view s) const {
		return parse_result(std::string(s), "");
	}
};

template<>
struct parser<guild_role> {
	explicit parser(const guild_text_message& t_msg):
		msg(t_msg) {}

	parse_result<const guild_role*> operator()(std::string_view s) {
		return parse_consecutive(s, "<@&"_p, uint64_parser(), '>'_p)
			   .transform(return_nth<1>())
			   .and_then([&](const uint64_t id) ->std::optional<const guild_role*> {
				   auto roles_in_msg = msg.mention_roles();
				   auto it = ranges::find(roles_in_msg, snowflake(id), &guild_role::id);
				   if (it == roles_in_msg.end()) {
					   return std::nullopt;
				   } else {
					   return &*it;
				   }
			   });
	}

	const guild_text_message& msg;
};

template<>
struct parser<int> {
	explicit parser(const guild_text_message&) {}

	parse_result<int> operator()(std::string_view s) const {
		return int_parser()(s);
	}
};

template<>
struct parser<uint32_t> {
	explicit parser(const guild_text_message&) {}

	parse_result<uint32_t> operator()(std::string_view s) const {
		return uint64_parser()(s);
	}
};

template<>
struct parser<uint64_t> {
	explicit parser(const guild_text_message&) {}

	parse_result<uint64_t> operator()(std::string_view s) const {
		return uint64_parser()(s);
	}
};

template<>
struct parser<int64_t> {
	explicit parser(const guild_text_message&) {}

	parse_result<int64_t> operator()(std::string_view s) const {
		return integral_parser<int64_t>()(s);
	}
};

template<typename T, int n>
struct parser<std::array<T, n>> {
	explicit parser(const guild_text_message& m):
		lower_parser(m) { }

	parse_result<std::array<T, n>> operator()(std::string_view s) {
		//requires default constructable tho ;-;
		std::array<T, n> stuffs;
		auto r1 = lower_parser(s);
		if (!r1) {
			return parse_fail();
		} else {
			stuffs[0] = r1.value();
			s = r1.rest();
		}

		for (auto i : ranges::views::iota(1, n)) {
			auto result = parse_consecutive(s, non_optional_whitespace_parser(), lower_parser).transform(return_nth<1>());
			if (!result) {
				return parse_fail();
			}

			s = result.rest();
			stuffs[i] = std::move(result.value());
		}

		return parse_result(std::move(stuffs), s);
	}

	parser<T> lower_parser;
};

template<typename T>
struct parser<std::vector<T>> {
	explicit parser(const guild_text_message& m):
		lower_parser(m) {}

	parse_result<std::vector<T>> operator()(std::string_view s) {
		return list_parser3(lower_parser, non_optional_whitespace_parser())(s);
	}

	parser<T> lower_parser;
};

//hmmm
// template<typename... Ts>
// struct parser<std::variant<Ts...>> {
// 	explicit parser(const guild_text_message& t_m):m(t_m) {
// 		
// 	}
//
// 	parse_result<std::variant<parse_result_value_t<parser<Ts>>...>> operator()(std::string_view s) const {
// 		auto p = parse_first_of(s, parser<Ts>(m)...);
//
// 		
// 		return parse_fail();
// 	}
//
// 	const guild_text_message& m;
// };

template<typename T>
struct parser<std::optional<T>> {
	using base = parser<T>;

	explicit parser(const guild_text_message& msg):
		lower_parser(msg) { }

	parse_result<std::optional<parse_result_value_t<parser<T>>>> operator()(std::string_view s) {
		return optional_parser(lower_parser)(s);
	}

	parser<T> lower_parser;

};

struct command {
	std::function<bool(std::string_view, guild_text_message&, shard&)> try_invoke_with;
};

struct command_group {
	command_group& operator+=(command c) {
		commands.push_back(std::move(c));
		return *this;
	}

	void call(std::string_view s, guild_text_message& msg, shard& sa) {
		for (const auto& fn : (commands) | ranges::views::transform(&command::try_invoke_with)) {
			if (fn(s, msg/* in/out might move*/, sa)) {
				return;
			}
		}
	}

	std::vector<command> commands;
};

//rename?
struct command_context {

	std::string prefix;

	void do_command(guild_text_message msg, shard& s) {
		if (msg.content().starts_with(prefix)) {
			auto content = msg.content();
			content.remove_prefix(prefix.size());
			//don't use string.find because i don't want npos, i want not-found to return the end
			const auto idx_of_whitespace_or_end = std::distance(content.begin(), std::find_if(content.begin(), content.end(), &isspace));
			const auto command_name = content.substr(0, idx_of_whitespace_or_end);

			const auto command_name_as_string = std::string(command_name);//usually SBO'd
			if (m_command_groups.find(command_name_as_string) != m_command_groups.end()) {
				content.remove_prefix(idx_of_whitespace_or_end);
				m_command_groups[command_name_as_string].call(content, msg, s);
			}
		}
	}

	auto& operator[](std::string_view s) {
		assert(std::none_of(s.begin(), s.end(), &isspace));
		return m_command_groups[std::string(s)];
	}

	auto& operator[](const std::string& s) {
		assert(std::none_of(s.begin(), s.end(), &isspace));
		return m_command_groups[s];
	}

	auto& operator[](const char* s) {//;-;
		return (*this)[std::string(s)];
	}
	
	const auto& groups() const noexcept {
		return m_command_groups;
	}

private:
	ska::bytell_hash_map<std::string, command_group> m_command_groups;
};

const auto to_something_passable = [](auto&& arg) ->auto&& {
	using type = std::remove_cvref_t<decltype(arg)>;
	if constexpr (std::is_pointer_v<type>) {
		return *arg;
	} else {
		return std::forward<decltype(arg)>(arg);
	}
};

template<parsable_arg... Format, typename fn>
command make_command(fn&& f) {
	command ret;
	if constexpr (sizeof...(Format) == 0) {
		ret.try_invoke_with = [f_ = std::forward<fn>(f)](std::string_view content, guild_text_message& c, shard& s) mutable {
			std::invoke(f_, std::move(c), s);
			return true;
		};
	} else {
		ret.try_invoke_with = [f_ = std::forward<fn>(f)](std::string_view content, guild_text_message& c, shard& s)mutable {
			auto stuff = parse_consecutive(
				content,
				transform_parser(multi_parser(non_optional_whitespace_parser(), parser<Format>(c)), ignore_left())...);

			if (!stuff) {
				return false;
			}

			std::apply([&](auto&&... args) {
				f_(to_something_passable(args)..., std::move(c), s);
			}, std::move(stuff.value()));
			return true;
		};
	}
	return ret;
}

}


//auto f = [](auto&&...){};


/*
 *commnand_context cc;
 *cc.prefix = "~~~"
 *cc["a"] += make_command<guild_member,role,string,word>([](const guild_member&,const guild_role&,std::string,guild_message,shard&){});
 * 
 */
