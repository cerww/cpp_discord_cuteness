#include "guild_role.h"

int guild_role::position() const noexcept { return m_position; }

const permission& guild_role::permissions() const noexcept { return m_permissions; }

snowflake guild_role::id() const noexcept { return m_id; }

bool guild_role::managed() const noexcept { return m_managed; }

bool guild_role::mentionable() const noexcept { return m_mentionable; }

bool guild_role::hoist() const noexcept { return m_hoist; }

std::string_view guild_role::name() const noexcept { return m_name; }

void to_json(nlohmann::json& json, const guild_role& r) {
	json["position"] = r.position();
	json["permissions"] = r.permissions();
	json["id"] = r.id();
	json["hoist"] = r.hoist();
	json["mentionable"] = r.mentionable();
	json["name"] = r.name();
	json["managed"] = r.managed();
}

void from_json(const nlohmann::json& json, guild_role& other) {
	if (json.is_null()) return;//why would role be null ;-;
	other.m_id = json["id"].get<snowflake>();
	other.m_position = json["position"].get<int>();
	other.m_permissions = json["permissions"].get<permission>();
	other.m_hoist = json["hoist"].get<bool>();
	other.m_managed = json["managed"].get<bool>();
	other.m_name = json["name"].get<std::string>();
	other.m_mentionable = json["mentionable"].get<bool>();
}
