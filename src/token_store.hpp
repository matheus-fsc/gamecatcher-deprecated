#pragma once

#include "auth.hpp"

#include <filesystem>
#include <optional>

namespace token_store {

// Linux: $XDG_CONFIG_HOME/gamecatcher (ou ~/.config/gamecatcher)
// Windows: %APPDATA%\gamecatcher
std::filesystem::path config_dir();

std::optional<auth::Credentials> load();
void save(const auth::Credentials& creds); // arquivo com permissão 0600
void remove();

} // namespace token_store
