#include "crypto/secrets/TPMKeyProvider.hpp"
#include "log/Registry.hpp"

#include <tss2/tss2_esys.h>
#include <fstream>
#include <filesystem>
#include <random>
#include <stdexcept>
#include <cstring>
#include <paths.h>

using namespace std::string_literals;

namespace vh::crypto::secrets {
    static std::filesystem::path resolveSealedDir(const std::string &key) {
        return paths::getBackingPath() / (".sealed_" + key + ".blob");
    }

    static ESYS_CONTEXT *init_esys() {
        ESYS_CONTEXT *ctx{};
        if (Esys_Initialize(&ctx, nullptr, nullptr) != TSS2_RC_SUCCESS) throw std::runtime_error(
            "Esys_Initialize failed");
        return ctx;
    }

    static std::vector<uint8_t> random_key(std::size_t n = 32) {
        std::vector<uint8_t> k(n);
        std::random_device rd;
        std::generate(k.begin(), k.end(), [&] { return static_cast<uint8_t>(rd()); });
        return k;
    }

    TPMKeyProvider::TPMKeyProvider(const std::string &name)
        : name_(name), sealedDir_(resolveSealedDir(name)) {
    }

    void TPMKeyProvider::init(const std::optional<std::string> &initSecret) {
        if (std::filesystem::exists(sealedDir_ / (name_ + ".priv"))) {
            unseal();
            return;
        }
        if (initSecret) masterKey_ = std::vector<uint8_t>(initSecret->begin(), initSecret->end());
        generate_and_seal();
    }

    const std::vector<uint8_t> &TPMKeyProvider::getMasterKey() const { return masterKey_; }

    static std::vector<uint8_t> slurp(const std::filesystem::path &p) {
        std::ifstream f(p, std::ios::binary);
        return {std::istreambuf_iterator<char>(f), {}};
    }

    static void dump(const std::filesystem::path &p, const std::vector<uint8_t> &buf) {
        std::filesystem::create_directories(p.parent_path());
        std::ofstream f(p, std::ios::binary);
        f.write(reinterpret_cast<const char *>(buf.data()), static_cast<long>(buf.size()));
    }

    static ESYS_TR create_primary(ESYS_CONTEXT *ctx) {
        TPM2B_SENSITIVE_CREATE sens{};
        sens.size = 0; // no auth, no extra sensitive data

        TPM2B_PUBLIC pub{};
        pub.size = 0; // let marshalling figure it out
        pub.publicArea.type = TPM2_ALG_RSA;
        pub.publicArea.nameAlg = TPM2_ALG_SHA256;
        pub.publicArea.objectAttributes =
                TPMA_OBJECT_RESTRICTED
                | TPMA_OBJECT_DECRYPT
                | TPMA_OBJECT_FIXEDTPM
                | TPMA_OBJECT_FIXEDPARENT
                | TPMA_OBJECT_SENSITIVEDATAORIGIN // required here
                | TPMA_OBJECT_USERWITHAUTH
                | TPMA_OBJECT_NODA;

        pub.publicArea.parameters.rsaDetail.symmetric.algorithm = TPM2_ALG_AES;
        pub.publicArea.parameters.rsaDetail.symmetric.keyBits.aes = 128;
        pub.publicArea.parameters.rsaDetail.symmetric.mode.aes = TPM2_ALG_CFB;
        pub.publicArea.parameters.rsaDetail.scheme.scheme = TPM2_ALG_NULL;
        pub.publicArea.parameters.rsaDetail.keyBits = 2048;
        pub.publicArea.parameters.rsaDetail.exponent = 0;

        TPM2B_DATA outside = {.size = 0, .buffer = {}};
        TPML_PCR_SELECTION pcr = {.count = 0, .pcrSelections = {}};

        ESYS_TR primHandle = ESYS_TR_NONE;
        TSS2_RC rc = Esys_CreatePrimary(
            ctx, ESYS_TR_RH_OWNER,
            ESYS_TR_PASSWORD, ESYS_TR_NONE, ESYS_TR_NONE,
            &sens, &pub, &outside, &pcr,
            &primHandle, nullptr, nullptr, nullptr, nullptr);
        if (rc != TSS2_RC_SUCCESS) {
            throw std::runtime_error("CreatePrimary failed rc=" + std::to_string(rc));
        }

        return primHandle;
    }

    void TPMKeyProvider::generate_and_seal() {
        if (masterKey_.empty()) masterKey_ = random_key();

        ESYS_CONTEXT *ctx = init_esys();
        ESYS_TR parent = create_primary(ctx);

        TPM2B_SENSITIVE_CREATE sens{};
        sens.size = 0;
        sens.sensitive.userAuth.size = 0;
        sens.sensitive.data.size = masterKey_.size();
        memcpy(sens.sensitive.data.buffer, masterKey_.data(), masterKey_.size());

        TPM2B_PUBLIC pub{};
        pub.size = 0;
        pub.publicArea.type = TPM2_ALG_KEYEDHASH;
        pub.publicArea.nameAlg = TPM2_ALG_SHA256;
        pub.publicArea.objectAttributes =
                TPMA_OBJECT_FIXEDTPM |
                TPMA_OBJECT_FIXEDPARENT |
                TPMA_OBJECT_USERWITHAUTH |
                TPMA_OBJECT_NODA;
        pub.publicArea.parameters.keyedHashDetail.scheme.scheme = TPM2_ALG_NULL;
        pub.publicArea.unique.keyedHash.size = 0;

        TPM2B_DATA outsideInfo{};
        TPML_PCR_SELECTION pcrSel{};

        TPM2B_PRIVATE *outPriv = nullptr;
        TPM2B_PUBLIC *outPub = nullptr;
        // ESYS_TR sealedObj = ESYS_TR_NONE;

        TSS2_RC rc = Esys_Create(ctx, parent,
                                 ESYS_TR_PASSWORD, ESYS_TR_NONE, ESYS_TR_NONE,
                                 &sens, &pub,
                                 &outsideInfo, &pcrSel,
                                 &outPriv, &outPub,
                                 nullptr, nullptr, nullptr);

        if (rc != TSS2_RC_SUCCESS) {
            Esys_FlushContext(ctx, parent);
            Esys_Finalize(&ctx);
            throw std::runtime_error("Esys_Create failed rc=" + std::to_string(rc));
        }

        dump(sealedDir_ / (name_ + ".priv"), {outPriv->buffer, outPriv->buffer + outPriv->size});
        dump(sealedDir_ / (name_ + ".pub"),
             {
                 reinterpret_cast<uint8_t *>(&outPub->publicArea),
                 reinterpret_cast<uint8_t *>(&outPub->publicArea) + sizeof(outPub->publicArea)
             });

        Esys_Free(outPriv);
        Esys_Free(outPub);

        // 🔥 Flush the created object too
        Esys_FlushContext(ctx, parent);

        Esys_Finalize(&ctx);

        log::Registry::crypto()->debug("[TPMKeyProvider] Sealed {} key to {}", name_, sealedDir_.string());
    }

    void TPMKeyProvider::unseal() {
        ESYS_CONTEXT *ctx = init_esys();
        ESYS_TR parent = create_primary(ctx); // recreate parent each boot

        TPM2B_PRIVATE inPriv{};
        auto privBuf = slurp(sealedDir_ / (name_ + ".priv"));
        inPriv.size = privBuf.size();
        memcpy(inPriv.buffer, privBuf.data(), privBuf.size());

        TPM2B_PUBLIC inPub{};
        auto pubBuf = slurp(sealedDir_ / (name_ + ".pub"));
        memcpy(&inPub.publicArea, pubBuf.data(), sizeof(inPub.publicArea));

        ESYS_TR obj;
        if (Esys_Load(ctx, parent,
                      ESYS_TR_PASSWORD, ESYS_TR_NONE, ESYS_TR_NONE,
                      &inPriv, &inPub, &obj) != TSS2_RC_SUCCESS)
            throw std::runtime_error("Esys_Load failed");

        TPM2B_SENSITIVE_DATA *outData;
        if (Esys_Unseal(ctx, obj,
                        ESYS_TR_PASSWORD, ESYS_TR_NONE, ESYS_TR_NONE,
                        &outData) != TSS2_RC_SUCCESS)
            throw std::runtime_error("Esys_Unseal failed");

        masterKey_.assign(outData->buffer, outData->buffer + outData->size);

        Esys_Free(outData);
        Esys_FlushContext(ctx, obj);
        Esys_FlushContext(ctx, parent);
        Esys_Finalize(&ctx);

        log::Registry::crypto()->debug("[TPMKeyProvider] Unsealed master key from {}", sealedDir_.string());
    }

    void TPMKeyProvider::updateMasterKey(const std::vector<uint8_t> &newMasterKey) {
        masterKey_ = newMasterKey;
        generate_and_seal();
    }

    bool TPMKeyProvider::sealedExists() const {
        const auto priv = sealedDir_ / (name_ + ".priv");
        const auto pub = sealedDir_ / (name_ + ".pub");
        return std::filesystem::exists(priv) && std::filesystem::exists(pub);
    }
} // namespace vh::crypto