#pragma once

// Atualização pelas releases do GitHub (API pública, sem login).
//
// Cada release publica, além dos pacotes, o binário solto de cada plataforma e a assinatura
// Ed25519 dele (.sig). A chave pública está embutida no programa e a privada só existe como
// segredo no CI: uma release adulterada (ou um download corrompido) é recusada antes de trocar
// qualquer arquivo.
//
// No Windows o programa não troca o próprio .exe: um .exe sem assinatura de código que baixa e
// substitui a si mesmo é bloqueado pelo Defender. Lá ele só avisa e abre a página da release, e a
// atualização é feita pelo install.bat do pacote novo.

#include <optional>
#include <string>

namespace update {

struct Release {
    std::string version;    // "1.2.0"
    std::string asset;      // nome do binário desta plataforma na release
    std::string binary_url;
    std::string sig_url;
    std::string page_url;   // página da release no GitHub
};

#ifdef _WIN32
constexpr bool kSelfUpdate = false;
#else
constexpr bool kSelfUpdate = true;
#endif

// A release mais nova, se for mais nova que esta versão (e, com kSelfUpdate, tiver binário assinado
// para esta plataforma).
std::optional<Release> check();

#ifndef _WIN32
// Baixa, confere a assinatura e troca o programa instalado. Lança em qualquer falha (nada é trocado).
void apply(const Release& r);
#endif

// Verificação automática: no máximo 1 por dia. true = já passou um dia (e marca agora).
bool due();

// Exposto para testes: confere `sig` (64 bytes) de `asset` + "\n" + `binary`.
bool verify(const std::string& asset, const std::string& binary, const std::string& sig);

} // namespace update
