#include "internal_shard.h"
#include "client.h"
#include "voice_channel.h"
#include "dm_channel.h"
#include "init_shard.h"
#include "voice_connect_impl.h"
#include "../common/executor_binder.h"
#include "unavailable_guild.h"
#include "presence_update.h"
//#include <boost/beast/>

//copy paste start
#if defined(_WIN32) || defined(_WIN64)
static constexpr const char* s_os = "Windows";
#elif defined(__APPLE__) || defined(__MACH__)
static constexpr const char * s_os = "macOS";
#elif defined(__linux__) || defined(linux) || defined(__linux)
static constexpr const char * s_os = "Linux";
#elif defined __FreeBSD__
static constexpr const char * s_os = "FreeBSD";
#elif defined(unix) || defined(__unix__) || defined(__unix)
static constexpr const char * s_os = "Unix";
#else
static constexpr const char* s_os = "\\u00AF\\\\_(\\u30C4)_\\/\\u00AF";  //shrug I dunno
#endif
//copy paste end

using namespace fmt::literals;

template<typename rng, typename U>
//[[maybe_unused]]
constexpr bool erase_first_quick(rng& range, U&& val) {
	const auto it = std::find(range.begin(), range.end(), std::forward<U>(val));
	if (it == range.end())
		return false;
	std::swap(range.back(), *it);
	range.pop_back();
	return true;
}

namespace cacheless {
template<typename T>
T* ptr_or_null(ref_stable_map<snowflake, T>& in, snowflake key) {
	if (key.val)
		return &in[key];
	return nullptr;
}


internal_shard::internal_shard(
	int shard_number, client* t_parent, boost::asio::io_context& ioc, std::string_view gateway, intents intent
) :
	shard(shard_number, t_parent, ioc, std::string(t_parent->auth_token())),
	m_heartbeat_context(*this),
	m_ioc(ioc),
	m_resolver(ioc),
	m_socket(strand(), m_ssl_ctx),
	m_intents(intent) {
	init_shard(shard_number, *this, ioc, gateway);
}

nlohmann::json internal_shard::presence() const {
	nlohmann::json retVal;
	retVal["since"];
	retVal["game"]["name"] = m_game_name;
	retVal["game"]["type"] = 0;
	retVal["status"] = enum_to_string(m_status);
	retVal["afk"] = m_status == Status::idle;

	return retVal;
}

cerwy::task<voice_connection> internal_shard::connect_voice(const voice_channel& channel) {
	const auto channel_id = channel.id;
	const auto guild_id = channel.guild_id;
	auto [endpoint, session_id_task] = m_opcode4(channel);
	auto ep_json = co_await endpoint;
	auto session_id = co_await session_id_task;
	std::cout << ep_json.dump(1);
	std::string gateway = "wss://"s + ep_json["endpoint"].get<std::string>() + "/?v=4"s;
	std::cout << gateway << std::endl;

	m_things_waiting_for_voice_endpoint2.erase(channel.guild_id);
	m_things_waiting_for_voice_endpoint.erase(channel.guild_id);
	co_return co_await voice_connect_impl(*this, channel, std::move(gateway), ep_json["token"].get<std::string>(), std::move(session_id));
}

cerwy::task<boost::beast::error_code> internal_shard::connect_http_connection() {
	auto ec = co_await m_http_connection.async_connect();
	int tries = 1;
	//TODO: do something else
	while (ec && tries < 10) {
		ec = co_await m_http_connection.async_connect();
		++tries;
	}
	co_return ec;
}

void internal_shard::doStuff(nlohmann::json stuffs, int op) {
	//fmt::print("{}\n", stuffs.dump(1));
	switch (op) {
	case 0:
		m_opcode0(std::move(stuffs["d"]), to_event_name(stuffs["t"]), stuffs["s"]);
		break;
	case 1:
		m_opcode1_send_heartbeat();
		break;
	case 2: break;
	case 3: break;
	case 4: break;
	case 5: break;
	case 6: break;
	case 7:
		m_opcode7_reconnect();
		break;
	case 8: break;
	case 9:
		m_opcode9_on_invalid_session(std::move(stuffs["d"]));
		break;
	case 10:
		m_opcode10_on_hello(stuffs["d"]);
		break;
	case 11:
		m_opcode11(stuffs["d"]);
		break;
	default: break;
	}
}

// ReSharper disable CppMemberFunctionMayBeConst
void internal_shard::on_reconnect() {
	// ReSharper restore CppMemberFunctionMayBeConst
	send_resume();
}

void internal_shard::rate_limit(std::chrono::system_clock::time_point tp) {
	m_http_connection.sleep_till(tp);
}

void internal_shard::close_connection(int code) {
	m_is_disconnected = true;
	m_client->close(boost::beast::websocket::close_reason(code));
}


void internal_shard::request_guild_members(Guild& g) const {
	m_opcode8_guild_member_chunk(g.id);
}

void internal_shard::m_opcode0(nlohmann::json data, event_name event, uint64_t s) {
	m_seq_num.store(std::max(s, m_seq_num.load(std::memory_order_relaxed)), std::memory_order_relaxed);
	std::cout << m_seq_num << std::endl;
	//std::cout << data << std::endl;
	//{
	//auto lock = co_await m_events_mut.async_lock();//?
	//}
	try {
		switch (event) {
		case event_name::HELLO:					procces_event<event_name::HELLO>(data);
			break;
		case event_name::READY:					procces_event<event_name::READY>(data);
			break;;
		case event_name::RESUMED:				procces_event<event_name::RESUMED>(data);
			break;;
		case event_name::INVALID_SESSION:		procces_event<event_name::INVALID_SESSION>(data);
			break;;
		case event_name::CHANNEL_CREATE:		procces_event<event_name::CHANNEL_CREATE>(data);
			break;;
		case event_name::CHANNEL_UPDATE:		procces_event<event_name::CHANNEL_UPDATE>(data);
			break;;
		case event_name::CHANNEL_DELETE:		procces_event<event_name::CHANNEL_DELETE>(data);
			break;;
		case event_name::CHANNEL_PINS_UPDATE:	procces_event<event_name::CHANNEL_PINS_UPDATE>(data);
			break;;
		case event_name::GUILD_CREATE:			procces_event<event_name::GUILD_CREATE>(data);
			break;;
		case event_name::GUILD_UPDATE:			procces_event<event_name::GUILD_UPDATE>(data);
			break;;
		case event_name::GUILD_DELETE:			procces_event<event_name::GUILD_DELETE>(data);
			break;;
		case event_name::GUILD_BAN_ADD:			procces_event<event_name::GUILD_BAN_ADD>(data);
			break;;
		case event_name::GUILD_BAN_REMOVE:		procces_event<event_name::GUILD_BAN_REMOVE>(data);
			break;;
		case event_name::GUILD_EMOJI_UPDATE:	procces_event<event_name::GUILD_EMOJI_UPDATE>(data);
			break;;
		case event_name::GUILD_INTEGRATIONS_UPDATE: procces_event<event_name::GUILD_INTEGRATIONS_UPDATE>(data);
			break;;
		case event_name::GUILD_MEMBER_ADD:		procces_event<event_name::GUILD_MEMBER_ADD>(data);
			break;;
		case event_name::GUILD_MEMBER_REMOVE:	procces_event<event_name::GUILD_MEMBER_REMOVE>(data);
			break;;
		case event_name::GUILD_MEMBER_UPDATE:	procces_event<event_name::GUILD_MEMBER_UPDATE>(data);
			break;;
		case event_name::GUILD_MEMBERS_CHUNK:	procces_event<event_name::GUILD_MEMBERS_CHUNK>(data);
			break;;
		case event_name::GUILD_ROLE_CREATE:		procces_event<event_name::GUILD_ROLE_CREATE>(data);
			break;;
		case event_name::GUILD_ROLE_UPDATE:		procces_event<event_name::GUILD_ROLE_UPDATE>(data);
			break;;
		case event_name::GUILD_ROLE_DELETE:		procces_event<event_name::GUILD_ROLE_DELETE>(data);
			break;;
		case event_name::MESSAGE_CREATE:		procces_event<event_name::MESSAGE_CREATE>(data);
			break;;
		case event_name::MESSAGE_UPDATE:		procces_event<event_name::MESSAGE_UPDATE>(data);
			break;;
		case event_name::MESSAGE_DELETE:		procces_event<event_name::MESSAGE_DELETE>(data);
			break;;
		case event_name::MESSAGE_DELETE_BULK:	procces_event<event_name::MESSAGE_DELETE_BULK>(data);
			break;;
		case event_name::MESSAGE_REACTION_ADD:	procces_event<event_name::MESSAGE_REACTION_ADD>(data);
			break;;
		case event_name::MESSAGE_REACTION_REMOVE: procces_event<event_name::MESSAGE_REACTION_REMOVE>(data);
			break;;
		case event_name::MESSAGE_REACTION_REMOVE_ALL: procces_event<event_name::MESSAGE_REACTION_REMOVE_ALL>(data);
			break;;
		case event_name::PRESENCE_UPDATE:		procces_event<event_name::PRESENCE_UPDATE>(data);
			break;;
		case event_name::TYPING_START:			procces_event<event_name::TYPING_START>(data);
			break;;
		case event_name::USER_UPDATE:			procces_event<event_name::USER_UPDATE>(data);
			break;;
		case event_name::VOICE_STATE_UPDATE:	procces_event<event_name::VOICE_STATE_UPDATE>(data);
			break;;
		case event_name::VOICE_SERVER_UPDATE:	procces_event<event_name::VOICE_SERVER_UPDATE>(data);
			break;;
		case event_name::WEBHOOKS_UPDATE:		procces_event<event_name::WEBHOOKS_UPDATE>(data);
			break;;

		}
	} catch (...) {
		//swallow
	}
}

void internal_shard::m_opcode1_send_heartbeat() const {
	if (is_disconnected())
		return;
	std::string t =
			R"({
	"op": 1,
	"d" : null
})";
	m_client->send_thing(std::move(t));
}

void internal_shard::m_opcode2_send_identity() const {
	m_send_identity();
}

void internal_shard::m_opcode3_send_presence() const {
	nlohmann::json val;
	val["op"] = 3;
	val["data"] = presence();
	m_client->send_thing(val.dump());
}

std::pair<cerwy::task<nlohmann::json>, cerwy::task<std::string>> internal_shard::m_opcode4(const voice_channel& channel) {
	cerwy::promise<nlohmann::json> name_promise;
	cerwy::promise<std::string> session_id_promise;

	auto name = name_promise.get_task();
	auto session_id_task = session_id_promise.get_task();

	m_things_waiting_for_voice_endpoint.insert(std::make_pair(channel.guild_id, std::move(name_promise)));
	m_things_waiting_for_voice_endpoint2.insert(std::make_pair(channel.guild_id, std::move(session_id_promise)));

	m_client->send_thing(
		R"(
{{
    "op": 4,
    "d": {{
        "guild_id": "{}",
        "channel_id": "{}",
        "self_mute": false,
        "self_deaf": false
    }}
}}
)"_format(channel.guild_id, channel.id));


	return {std::move(name), std::move(session_id_task)};
}

void internal_shard::m_opcode6_send_resume() const {
	send_resume();
}

void internal_shard::m_opcode7_reconnect() {
	reconnect();
}

void internal_shard::m_opcode8_guild_member_chunk(snowflake id) const {
	nlohmann::json s;
	s["op"] = 8;
	s["d"]["guild_id"] = std::to_string(id.val);
	s["d"]["query"] = "";
	s["d"]["limit"] = 0;
	std::cout << s << std::endl;
	m_client->send_thing(s.dump());
}

cerwy::task<void> internal_shard::m_opcode9_on_invalid_session(nlohmann::json d) {
	boost::asio::steady_timer timer(strand(), std::chrono::steady_clock::now() + 5s);
	auto ec = co_await timer.async_wait(use_task_return_ec);

	if (d.get<bool>())
		m_opcode6_send_resume();
	else
		m_opcode2_send_identity();

	/*
	std::this_thread::sleep_for(5s);

	if (d.get<bool>())
		m_opcode6_send_resume();
	else
		m_opcode2_send_identity();

	*/
}


void internal_shard::m_opcode10_on_hello(nlohmann::json& stuff) {
	m_heartbeat_context.hb_interval = stuff["heartbeat_interval"].get<int>();
	m_heartbeat_context.start();
	m_send_identity();
}

void internal_shard::m_opcode11(nlohmann::json& data) {
	//m_op11 = true;
	m_heartbeat_context.recived_ack = true;
}

void internal_shard::reconnect() {
	if (!is_disconnected())
		close_connection(1001);
	//m_parent->reconnect(this, m_shard_number);
	//reconnect will happen in init_shard
}

void internal_shard::send_resume() const {
	std::string temp(R"({
		"op":6,
		"d":{
			"token":)"s + std::string(m_parent->token()) + R"(
			"session_id:)"s + m_session_id + R"(
			"seq:")" + std::to_string(m_seq_num) + R"(
		}
	}
	)"s);
	m_client->send_thing(std::move(temp));
}

void internal_shard::apply_presences(const nlohmann::json& presences, Guild& guild) {
	for (const auto& presence : presences) {
		const auto id = presence["user"]["id"].get<snowflake>();
		const auto& temp = presence["game"];
		if (!temp.is_null()) {
			guild.activities.insert(std::make_pair(id, std::optional<activity>(temp.get<activity>())));
		}
		//guild.status[id] = string_to_status(presence["status"].get_ref<const nlohmann::json::string_t&>());
	}
}

void internal_shard::replay_events_for(snowflake guild_id) {
	auto it = m_backed_up_events.find(guild_id);
	if (it != m_backed_up_events.end()) {
		auto backed_up_events = std::move(it->second);
		m_backed_up_events.erase(it);
		for (auto& event : backed_up_events) {
			m_opcode0(event.first, event.second, 0);
		}
	} else {
		//???????????
	}
}

void internal_shard::m_send_identity() const {
	auto identity = fmt::format(R"(
{{
"op":2,
"d":{{
	"token":"{}",
	"properties":{{
		"$os":"{}",
		"$browser":"cerwy",
		"$device":"cerwy"
	}},
	"compress":false,
	"large_threashold":{},
	"shard":[{},{}],
	"presence":{},
	"intents":{}
}}
}}
)", m_parent->token(), s_os, large_threshold, m_shard_number, m_parent->num_shards(), presence().dump(), m_intents.int_value());

	auto timer = std::make_unique<boost::asio::steady_timer>(m_ioc);
	timer->expires_at(m_parent->get_time_point_for_identifying());
	timer->async_wait([pin = std::move(timer), str = std::move(identity),this](auto ec)mutable {
		m_client->send_thing(std::move(str));
	});
}

template<>
void internal_shard::procces_event<event_name::READY>(nlohmann::json& event) {
	m_trace2 = event["_trace"];
	m_self_user = event["user"].get<user>();
	m_session_id = event["session_id"].get<std::string>();
	//ignore unavailable guilds, only need size
	m_guilds.reserve(event["guilds"].size());


	//no longer needed, since event["private_channels"] will now always be an empty array 
	/*
	const auto& private_channels = event["private_channels"].get_ref<const nlohmann::json::array_t&>();

	m_dm_channels = private_channels | ranges::views::transform(&get_then_return_id<dm_channel>) | ranges::to<ref_stable_map<snowflake,dm_channel>>();
	*/

	//m_parent->on_ready(*this);

}

template<>
void internal_shard::procces_event<event_name::GUILD_CREATE>(nlohmann::json& data) {
	// std::cout << data["id"] << std::endl;
	// Guild& guild = m_guilds.insert(std::make_pair(data["id"].get<snowflake>(), data.get<Guild>())).first->second;
	// if (m_intents.has_intents(intent::GUILD_MEMBERS) && guild.m_member_count >= large_threshold) {
	// 	guild.m_is_ready = false;
	// 	request_guild_members(guild);
	// } else if (m_intents.has_intents(intent::GUILD_MEMBERS)) {
	// 	for (const auto& member_json : data["members"]) {
	// 		auto member = member_json.get<guild_member>();
	// 		member.m_guild = &guild;
	// 		const auto id = member.id();
	// 		guild.m_members.insert(std::make_pair(id, std::move(member)));
	// 	}
	// }
	//
	// const auto channels = data["channels"].get_ref<const nlohmann::json::array_t&>();
	// guild.m_text_channel_ids.reserve(channels.size());
	// guild.m_voice_channel_ids.reserve(std::max(3ull, channels.size() / 10));//random numbers
	//
	// for (const auto& channel_json : channels) {
	// 	if (const auto type = channel_json["type"].get<int>(); type == 0 || type == 5 || type == 6) {//text
	// 		auto& channel = m_text_channel_map.insert(std::make_pair(channel_json["id"].get<snowflake>(), channel_json.get<text_channel>())).first->second;
	// 		channel.m_guild_id = guild.id();
	// 		guild.m_text_channel_ids.push_back(channel.id());
	// 		channel.m_guild = &guild;
	// 		if (type == 5) {
	// 			channel.m_channel_type = text_channel_type::news;
	// 		}
	//
	// 		if (type == 6) {
	// 			channel.m_channel_type = text_channel_type::store;
	// 		}
	// 	} else if (type == 2) {//voice
	// 		auto& channel = m_voice_channel_map.insert(std::make_pair(channel_json["id"].get<snowflake>(), channel_json.get<voice_channel>())).first->second;
	// 		channel.m_guild_id = guild.id();
	// 		guild.m_voice_channel_ids.push_back(channel.id());
	// 		channel.m_guild = &guild;
	// 	} else if (type == 4) {//guild catagory
	// 		auto& channel = m_channel_catagory_map.insert(std::make_pair(channel_json["id"].get<snowflake>(), channel_json.get<channel_catagory>())).first->second;
	// 		channel.m_guild_id = guild.id();
	// 		guild.m_channel_catagory_ids.push_back(channel.id());
	// 		channel.m_guild = &guild;
	// 	} else {
	// 		//unimplemented channel
	// 	}
	//
	// 	//type == 1,3 is missing since it's DM channel, dm channels don't exist in guild channels
	// }
	//
	// for (auto& channel : guild.m_text_channel_ids |
	// 	 ranges::views::transform(hof::map_with(m_text_channel_map)) |
	// 	 ranges::views::filter(&text_channel::has_parent)) {
	// 	channel.m_parent = &m_channel_catagory_map[channel.m_parent_id];
	// }
	//
	// for (const auto& channel_id : guild.m_voice_channel_ids) {
	// 	auto& channel = m_voice_channel_map[channel_id];
	// 	if (channel.m_parent_id.val)
	// 		channel.m_parent = &m_channel_catagory_map[channel.m_parent_id];
	// }
	//
	// if (m_intents.has_intents(intent::GUILD_PRESENCES)) {
	// 	if (guild.m_is_ready) {
	// 		apply_presences(data["presences"], guild);
	// 	} else {
	// 		guild.m_presences = std::move(data["presences"]);//save it for later
	// 	}
	// }
	// guild.m_shard = this;
	// if (guild.m_is_ready) {
	// 	m_parent->on_guild_ready(guild, *this);
	// }

	auto guild = data.get<Guild>();
	// 	m_parent->on_guild_ready(guild, *this);
	
}

//TODO
//New Properties on Guild Members Chunk Event
//April 24, 2020
//The Guild Members Chunk gateway event now contains two properties : chunk_index and chunk_count.
//These values can be used to keep track of how many events you have left to receive in response to a Request Guild Members command.

template<>
void internal_shard::procces_event<event_name::GUILD_MEMBERS_CHUNK>(nlohmann::json& e) {

	auto members = e["members"].get<std::vector<guild_member>>();
	int chunk_count = e["chunk_count"].get<int>();
	int chunk_index = e["chunk_index"].get<int>();
	auto guild_id = e["guild_id"].get<snowflake>();
	
	
}


template<>
void internal_shard::procces_event<event_name::MESSAGE_CREATE>(nlohmann::json& e) {
	const auto channel_id = e["channel_id"].get<snowflake>();
	const bool is_guild_msg = e.contains("guild_id");
	if (is_guild_msg) {

		auto msg = create_msg<guild_text_message>(e);

		m_parent->on_guild_text_msg(std::move(msg), *this);
	} else {//dm msg
		
		auto msg = create_msg<dm_message>(e);
		m_parent->on_dm_msg(std::move(msg), *this);
	}
}

template<>
void internal_shard::procces_event<event_name::CHANNEL_CREATE>(nlohmann::json& channel_json) {
	const auto type = channel_json["type"].get<int>();
	if (type == 0 || type == 5 || type == 6) {//text
		auto channel = channel_json.get<text_channel>();// guild.text_channels.insert(std::make_pair(channel_json["id"].get<snowflake>(), )).first->second;


		if (type == 5) {
			channel.channel_type = text_channel_type::news;
		}

		if (type == 6) {
			channel.channel_type = text_channel_type::store;
		}
		//m_parent->on_guild_text_channel_create(std::move(channel),*this);
	}
	else if (type == 1 || type == 3) {//DM
		auto channel = channel_json.get<dm_channel>();
		
	}else if (type == 2) {//voice
		auto channel = channel_json.get<voice_channel>();
		
	}
	else if (type == 4) {//guild catagory
		auto channel = channel_json.get<channel_catagory>();
		
	}
	else {
		//unimplemented channel
	}

	
}

template<>
void internal_shard::procces_event<event_name::CHANNEL_DELETE>(nlohmann::json& e) {
	const auto type = e["type"].get<int>();
	if (type == 0 || type == 5 || type == 6) {//text
		auto channel = e.get<text_channel>();// guild.text_channels.insert(std::make_pair(channel_json["id"].get<snowflake>(), )).first->second;


		if (type == 5) {
			channel.channel_type = text_channel_type::news;
		}

		if (type == 6) {
			channel.channel_type = text_channel_type::store;
		}
		//m_parent->on_guild_text_channel_create(std::move(channel),*this);
	}
	else if (type == 1 || type == 3) {//DM
		auto channel = e.get<dm_channel>();

	}
	else if (type == 2) {//voice
		auto channel = e.get<voice_channel>();

	}
	else if (type == 4) {//guild catagory
		auto channel = e.get<channel_catagory>();

	}
	else {
		//unimplemented channel
	}

}

template<>
void internal_shard::procces_event<event_name::GUILD_MEMBER_ADD>(nlohmann::json& e) {
	auto guild_id = e["guild_id"].get<snowflake>();
	auto member = e.get<guild_member>();
}

template<>
void internal_shard::procces_event<event_name::GUILD_MEMBER_REMOVE>(nlohmann::json& e) {
	auto guild_id = e["guild_id"].get<snowflake>();
	user person = e["user"].get<user>();
	
}

template<>
void internal_shard::procces_event<event_name::RESUMED>(nlohmann::json& e) {
	m_trace = e["_trace"].get<std::vector<std::string>>();
};

template<>
void internal_shard::procces_event<event_name::HELLO>(nlohmann::json& json) {
	m_trace = json.at("_trace").get<std::vector<std::string>>();
	m_heartbeat_context.hb_interval = json["heartbeat_interval"].get<int>();
};

template<>
void internal_shard::procces_event<event_name::INVALID_SESSION>(nlohmann::json&) { };

template<>
void internal_shard::procces_event<event_name::CHANNEL_UPDATE>(nlohmann::json& e) {
	const auto type = e["type"].get<int>();
	if (type == 0 || type == 5 || type == 6) {//text
		auto channel = e.get<text_channel>();// guild.text_channels.insert(std::make_pair(channel_json["id"].get<snowflake>(), )).first->second;


		if (type == 5) {
			channel.channel_type = text_channel_type::news;
		}

		if (type == 6) {
			channel.channel_type = text_channel_type::store;
		}
		//m_parent->on_guild_text_channel_create(std::move(channel),*this);
	}
	else if (type == 1 || type == 3) {//DM
		auto channel = e.get<dm_channel>();

	}
	else if (type == 2) {//voice
		auto channel = e.get<voice_channel>();

	}
	else if (type == 4) {//guild catagory
		auto channel = e.get<channel_catagory>();

	}
	else {
		//unimplemented channel
	}
}

template<>
void internal_shard::procces_event<event_name::CHANNEL_PINS_UPDATE>(nlohmann::json& e) {};

template<>
void internal_shard::procces_event<event_name::GUILD_UPDATE>(nlohmann::json& e) {
	const auto guild_id = e["id"].get<snowflake>();
	
	auto guild = e.get<partial_guild>();
	//m_parent->on_guild_update(g, *this);
};

template<>
void internal_shard::procces_event<event_name::GUILD_DELETE>(nlohmann::json& e) {
	try {
		auto guild = e.get<unavailable_guild>();
		
		//m_parent->on_guild_remove(g, unavailable, *this);
	} catch (...) {
		//swallow		 
	}
}

template<>
void internal_shard::procces_event<event_name::GUILD_BAN_ADD>(nlohmann::json& e) {
	auto member = e.get<user>();
	auto guild_id = e["guild_id"].get<snowflake>();
	//m_parent->on_ban_add(g, std::move(member), *this);
};

template<>
void internal_shard::procces_event<event_name::GUILD_BAN_REMOVE>(nlohmann::json& e) {
	auto member = e.get<user>();
	auto guild_id = e["guild_id"].get<snowflake>();
	//m_parent->on_ban_remove(g, std::move(member), *this);
}

template<>
void internal_shard::procces_event<event_name::GUILD_EMOJI_UPDATE>(nlohmann::json& e) {
	const auto guild_id = e["guild_id"].get<snowflake>();
	std::vector<emoji> emojis = e["emojis"].get<std::vector<emoji>>();
};

template<>
void internal_shard::procces_event<event_name::GUILD_INTEGRATIONS_UPDATE>(nlohmann::json& event) {
	//what is this ;-;
	//only has 1 field: id
	//wat
	const auto guild_id = event["id"].get<snowflake>();
};

template<>
void internal_shard::procces_event<event_name::GUILD_MEMBER_UPDATE>(nlohmann::json& e) {
	const auto guild_id = e["guild_id"].get<snowflake>();

	auto user = e["user"].get<cacheless::user>();
	auto roles = e["roles"].get<std::vector<guild_role>>();
	auto nick = e.value("nick", std::optional<std::string>());

	auto member_update = guild_member_update{ std::move(user),std::move(roles),std::move(nick),guild_id };
	

};

template<>
void internal_shard::procces_event<event_name::GUILD_ROLE_CREATE>(nlohmann::json& e) {
	auto guild_id = e["guild_id"].get<snowflake>();
	auto new_role = e["role"].get<guild_role>();
	//m_parent->on_role_create(guild, guild.m_roles.insert(std::make_pair(e["role"]["id"].get<snowflake>(), )).first->second, *this);
};

template<>
void internal_shard::procces_event<event_name::GUILD_ROLE_UPDATE>(nlohmann::json& e) {
	auto guild_id = e["guild_id"].get<snowflake>();
	auto updated_role = e["role"].get<guild_role>();
};

template<>
void internal_shard::procces_event<event_name::GUILD_ROLE_DELETE>(nlohmann::json& e) {
	auto guild_id = e["guild_id"].get<snowflake>();
	auto role_id = e["role_id"].get<snowflake>();
};

template<>
void internal_shard::procces_event<event_name::MESSAGE_UPDATE>(nlohmann::json& e) {
	
	const auto channel_id = e["channel_id"].get<snowflake>();
	const bool is_guild_msg = e.contains("guild_id");
	if (is_guild_msg) {

		auto msg = createMsgUpdate<guild_msg_update>(e);

		//m_parent->on_guild_text_msg(std::move(msg), *this);
	}
	else {//dm msg

		auto msg = createMsgUpdate<dm_msg_update>(e);
		//m_parent->on_dm_msg(std::move(msg), *this);
	}
}

template<>
void internal_shard::procces_event<event_name::MESSAGE_DELETE>(nlohmann::json& e) {
	auto id = e["id"].get<snowflake>();
	auto channel_id = e["channel_id"].get<snowflake>();
	auto guild_id = e.value("guild_id", snowflake());
	if(guild_id!=snowflake()) {
		
	}else {
		
	}
};

template<>
void internal_shard::procces_event<event_name::MESSAGE_DELETE_BULK>(nlohmann::json& e) {
	auto msg_ids = e["ids"].get<std::vector<snowflake>>();
	auto channel_id = e["channel_id"].get<snowflake>();
	auto guild_id = e.value("guild_id", snowflake());
	if (guild_id != snowflake()) {

	}else {

	}
};

template<>
void internal_shard::procces_event<event_name::MESSAGE_REACTION_ADD>(nlohmann::json& e) {
	const auto channel_id = e["channel_id"].get<snowflake>();
	auto emojiy = e["emoji"].get<partial_emoji>();
	const bool is_guild = e.contains("guild_id");	
	const auto message_id = e["message_id"].get<snowflake>();

	if (is_guild) {
		auto member = e["member"].get<guild_member>();
		
		//m_parent->on_guild_reaction_add(member, channel, e["message_id"].get<snowflake>(), std::move(emojiy), *this);
	} else {
		auto user_id = e["user_id"].get<snowflake>();
		
	}
};

template<>
void internal_shard::procces_event<event_name::MESSAGE_REACTION_REMOVE>(nlohmann::json& e) {
	const auto channel_id = e["channel_id"].get<snowflake>();
	auto emojiy = e["emoji"].get<partial_emoji>();
	const bool is_guild = e.contains("guild_id");
	const auto message_id = e["message_id"].get<snowflake>();

	if (is_guild) {
		auto user_id = e["user_id"].get<snowflake>();

		//m_parent->on_guild_reaction_add(member, channel, e["message_id"].get<snowflake>(), std::move(emojiy), *this);
	}else {
		auto user_id = e["user_id"].get<snowflake>();

	}
};

template<>
void internal_shard::procces_event<event_name::MESSAGE_REACTION_REMOVE_ALL>(nlohmann::json& e) {
	const auto channel_id = e["channel_id"].get<snowflake>();
	const auto message_id = e["message_id"].get<snowflake>();
	const auto guild_id = e.value("guild_id", snowflake());
	if(guild_id.val!=0) {
		
	}else {
		
	}

};

template<>
void internal_shard::procces_event<event_name::PRESENCE_UPDATE>(nlohmann::json& e) {
	auto update_event = e.get<presence_update_event>();
	
};

template<>
void internal_shard::procces_event<event_name::TYPING_START>(nlohmann::json& e) {
	const auto channel_id = e["channel_id"].get<snowflake>();
	const auto guild_id = e.value("guild_id",snowflake());
	
	
	if (guild_id != snowflake()) {

		auto member = e["member"].get<guild_member>();//if we have GUILD_MEMBER intent, just copy from guild.m_members[id]?
		
		
		//m_parent->on_guild_typing_start(std::move(member), it->second, *this);
	} else {
		auto user_id = e["user_id"].get<snowflake>();
		//m_parent->on_dm_typing_start(member, channel, *this);
	}
};

template<>
void internal_shard::procces_event<event_name::USER_UPDATE>(nlohmann::json& e) {
	m_self_user = e.get<user>();
};

template<>
void internal_shard::procces_event<event_name::VOICE_STATE_UPDATE>(nlohmann::json& e) {

	const auto guild_id = e.value("guild_id", snowflake());
	//Guild& guild = guild_id.val ? m_guilds[guild_id] : *m_voice_channel_map[e["channel_id"].get<snowflake>()].m_guild;

	const auto user_id = e.value("user_id", self_user().id);
	auto voice_state = e.get<cacheless::voice_state>();
	

	if (user_id == self_user().id) {
		auto it = m_things_waiting_for_voice_endpoint2.find(guild_id);
		if (it != m_things_waiting_for_voice_endpoint2.end()) {
			it->second.set_value(e["session_id"].get<std::string>());
		}
	}
	
};

template<>
void internal_shard::procces_event<event_name::VOICE_SERVER_UPDATE>(nlohmann::json& json) {
	const auto guild_id = json["guild_id"].get<snowflake>();
	m_things_waiting_for_voice_endpoint[guild_id].set_value(std::move(json));
};

template<>
void internal_shard::procces_event<event_name::WEBHOOKS_UPDATE>(nlohmann::json&) { }

// ReSharper disable once CppMemberFunctionMayBeConst
void internal_shard::update_presence(const Status s, std::string g) {// NOLINT
	m_status = s;
	m_game_name = std::move(g);
	m_opcode3_send_presence();
}

}