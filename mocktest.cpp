// SPDX-FileCopyrightText: 2020 Nicolás Alvarez <nicolas.alvarez@gmail.com>
//
// SPDX-License-Identifier: Apache-2.0

#include <gtest/gtest.h>

#include "mockssl.h"

#define WIN32_LEAN_AND_MEAN
#define SECURITY_WIN32
#include <windows.h>
#include <sspi.h>

extern "C"
PSecurityFunctionTableW SEC_ENTRY OchannelInitSecurityInterface();

using ::testing::_;
using ::testing::Return;
using ::testing::InSequence;

class Fixture : public ::testing::Test {
protected:
    PSecurityFunctionTableW funcTable;

    Fixture() {
        funcTable = OchannelInitSecurityInterface();
    }
};

TEST_F(Fixture, CredentialsHandleCreate) {
    OpenSSLMock openssl;

    SSL_CTX* ctx;
    EXPECT_CALL(openssl, SSL_CTX_new(_)).WillOnce([&](auto meth) { return ctx = new ssl_ctx_st(meth); });

    CredHandle cred;
    funcTable->AcquireCredentialsHandleW(nullptr, nullptr, 0, nullptr, nullptr, nullptr, nullptr, &cred, nullptr);

    EXPECT_CALL(openssl, SSL_CTX_free(ctx));
    funcTable->FreeCredentialsHandle(&cred);
}

std::ostream& operator<<(std::ostream& os, const SecBuffer& buf) {
    if (buf.pvBuffer == nullptr) {
        return os << "[null buffer]";
    } else {
        return os << "SecBuffer len " << buf.cbBuffer << " content '" << std::string((const char*)buf.pvBuffer, buf.cbBuffer) << "'";
    }
}

bool operator==(const SecBuffer& buf, const std::string& s) {
    return buf.pvBuffer != nullptr && std::string((const char*)buf.pvBuffer, buf.cbBuffer) == s;
}

class FixtureWithCredHandle : public Fixture {
protected:
    OpenSSLMock openssl;
    SSL_CTX* opensslCtx;
    CredHandle sspCred;

    void SetUp() {
        EXPECT_CALL(openssl, SSL_CTX_new(_)).WillOnce([&](auto meth) { return opensslCtx = new ssl_ctx_st(meth); });
        funcTable->AcquireCredentialsHandleW(nullptr, nullptr, 0, nullptr, nullptr, nullptr, nullptr, &sspCred, nullptr);
    }
    void TearDown() {
        EXPECT_CALL(openssl, SSL_CTX_free(opensslCtx));
        funcTable->FreeCredentialsHandle(&sspCred);
    }
};

TEST_F(FixtureWithCredHandle, InitContext) {

    CtxtHandle sspCtx{};
    SSL sslObject(opensslCtx);
    EXPECT_CALL(openssl, SSL_new(_)).WillOnce(Return(&sslObject));

    SecBufferDesc outputBufs{};
    SecBuffer outputBuf{};
    outputBufs.ulVersion = SECBUFFER_VERSION;
    outputBufs.cBuffers = 1;
    outputBufs.pBuffers = &outputBuf;

    const unsigned long REQ_FLAGS = ISC_REQ_SEQUENCE_DETECT | ISC_REQ_REPLAY_DETECT | ISC_REQ_CONFIDENTIALITY | ISC_REQ_ALLOCATE_MEMORY | ISC_REQ_STREAM;
    const unsigned long RET_FLAGS = ISC_RET_SEQUENCE_DETECT | ISC_RET_REPLAY_DETECT | ISC_RET_CONFIDENTIALITY | ISC_RET_ALLOCATED_MEMORY | ISC_RET_STREAM;
    unsigned long contextAttr;

    // first call, creates context and returns first output buffer
    EXPECT_CALL(sslObject, connect()).WillOnce([&] {
        sslObject.wbio->writestr("[ClientHello]");
        sslObject.last_error = SSL_ERROR_WANT_READ;
        return -1;
    });
    int retval = funcTable->InitializeSecurityContextW(
        &sspCred,       // phCredential
        nullptr,        // phContext
        nullptr,        // pszTargetName
        REQ_FLAGS,      // fContextReq
        0,              // Reserved1
        0,              // TargetDataRep
        nullptr,        // pInput
        0,              // Reserved2
        &sspCtx,        // phNewContext
        &outputBufs,    // pOutput
        &contextAttr,   // pfContextAttr
        nullptr         // ptsExpiry
    );
    ASSERT_EQ(outputBufs.pBuffers[0], "[ClientHello]");
    ASSERT_EQ(outputBufs.pBuffers[0].BufferType, SECBUFFER_TOKEN);
    ASSERT_EQ(retval, SEC_I_CONTINUE_NEEDED);
    funcTable->FreeContextBuffer(outputBufs.pBuffers[0].pvBuffer);
    outputBufs.pBuffers[0].pvBuffer = nullptr;

    SecBufferDesc inputBufs{};
    SecBuffer inputBuf[2]{};
    inputBufs.ulVersion = SECBUFFER_VERSION;
    inputBufs.cBuffers = 2;
    inputBufs.pBuffers = &inputBuf[0];
    inputBuf[0].BufferType = SECBUFFER_TOKEN;
    inputBuf[0].cbBuffer = 13;
    inputBuf[0].pvBuffer = "[ServerHello]";
    inputBuf[1].BufferType = SECBUFFER_EMPTY;

    // second call, we give it the existing context and the input buffer
    std::string tmpstr;
    EXPECT_CALL(sslObject, connect()).WillOnce([&] {
        tmpstr = sslObject.rbio->readstr();
        sslObject.wbio->writestr("[ClientKeyExchange]");
        sslObject.last_error = SSL_ERROR_WANT_READ;
        return -1;
    });
    retval = funcTable->InitializeSecurityContextW(
        &sspCred,       // phCredential
        &sspCtx,        // phContext
        nullptr,        // pszTargetName
        REQ_FLAGS,      // fContextReq
        0,              // Reserved1
        0,              // TargetDataRep
        &inputBufs,     // pInput
        0,              // Reserved2
        nullptr,        // phNewContext
        &outputBufs,    // pOutput
        &contextAttr,   // pfContextAttr
        nullptr         // ptsExpiry
    );
    ASSERT_EQ(tmpstr, "[ServerHello]");
    ASSERT_EQ(outputBufs.pBuffers[0], "[ClientKeyExchange]");
    ASSERT_EQ(retval, SEC_I_CONTINUE_NEEDED);

    // final call, handshake complete
    inputBuf[0].cbBuffer = 10;
    inputBuf[0].pvBuffer = "[Finished]";

    EXPECT_CALL(sslObject, connect()).WillOnce([&] {
        tmpstr = sslObject.rbio->readstr();
        sslObject.last_error = 0;
        return 1;
    });
    retval = funcTable->InitializeSecurityContextW(
        &sspCred,       // phCredential
        &sspCtx,        // phContext
        nullptr,        // pszTargetName
        REQ_FLAGS,      // fContextReq
        0,              // Reserved1
        0,              // TargetDataRep
        &inputBufs,     // pInput
        0,              // Reserved2
        nullptr,        // phNewContext
        &outputBufs,    // pOutput
        &contextAttr,   // pfContextAttr
        nullptr         // ptsExpiry
    );
    ASSERT_EQ(tmpstr, "[Finished]");
    ASSERT_EQ(retval, SEC_E_OK);
    ASSERT_EQ(contextAttr, RET_FLAGS);

    EXPECT_CALL(openssl, SSL_free(&sslObject));
    funcTable->DeleteSecurityContext(&sspCtx);
}

TEST_F(FixtureWithCredHandle, EncryptData) {
    // Initialize context with as little code as possible
    CtxtHandle sspCtx{};
    SSL sslObject(opensslCtx);
    EXPECT_CALL(openssl, SSL_new(_)).WillOnce(Return(&sslObject));

    SecBufferDesc outputBufs{};
    SecBuffer outputBuf{};
    outputBufs.ulVersion = SECBUFFER_VERSION;
    outputBufs.cBuffers = 1;
    outputBufs.pBuffers = &outputBuf;

    const unsigned long REQ_FLAGS = ISC_REQ_SEQUENCE_DETECT | ISC_REQ_REPLAY_DETECT | ISC_REQ_CONFIDENTIALITY | ISC_REQ_ALLOCATE_MEMORY | ISC_REQ_STREAM;
    unsigned long contextAttr;

    EXPECT_CALL(sslObject, connect()).WillOnce([&] {
        sslObject.wbio->writestr("[Magic]");
        return 1;
    });
    int retval = funcTable->InitializeSecurityContextW(
        &sspCred,       // phCredential
        nullptr,        // phContext
        nullptr,        // pszTargetName
        REQ_FLAGS,      // fContextReq
        0,              // Reserved1
        0,              // TargetDataRep
        nullptr,        // pInput
        0,              // Reserved2
        &sspCtx,        // phNewContext
        &outputBufs,    // pOutput
        &contextAttr,   // pfContextAttr
        nullptr         // ptsExpiry
    );
    ASSERT_EQ(outputBufs.pBuffers[0], "[Magic]");
    ASSERT_EQ(outputBufs.pBuffers[0].BufferType, SECBUFFER_TOKEN);
    ASSERT_EQ(retval, SEC_E_OK);
    funcTable->FreeContextBuffer(outputBufs.pBuffers[0].pvBuffer);

    SecPkgContext_StreamSizes streamSizes{};
    retval = funcTable->QueryContextAttributesW(&sspCtx, SECPKG_ATTR_STREAM_SIZES, &streamSizes);
    ASSERT_EQ(retval, SEC_E_OK);

    SecBufferDesc dataBufDesc{};
    SecBuffer dataBuf[4]{};
    std::unique_ptr<char[]> buf = std::make_unique<char[]>(10 + streamSizes.cbHeader + streamSizes.cbTrailer);

    dataBuf[0].BufferType = SECBUFFER_STREAM_HEADER;
    dataBuf[0].cbBuffer = streamSizes.cbHeader;
    dataBuf[0].pvBuffer = &buf[0];
    dataBuf[1].BufferType = SECBUFFER_DATA;
    dataBuf[1].cbBuffer = 10;
    dataBuf[1].pvBuffer = &buf[streamSizes.cbHeader];
    dataBuf[2].BufferType = SECBUFFER_STREAM_TRAILER;
    dataBuf[2].cbBuffer = streamSizes.cbTrailer;
    dataBuf[2].pvBuffer = &buf[streamSizes.cbHeader+10];
    dataBuf[3].BufferType = SECBUFFER_EMPTY;
    dataBuf[3].cbBuffer = 0;
    dataBuf[3].pvBuffer = nullptr;
    dataBufDesc.ulVersion = SECBUFFER_VERSION;
    dataBufDesc.cBuffers = 4;
    dataBufDesc.pBuffers = dataBuf;
    memcpy(dataBuf[1].pvBuffer, "helloworld", 10);

    EXPECT_CALL(sslObject, write(_, _)).WillOnce([&](const void* p, int len) {
        EXPECT_EQ(std::string((const char*)p, len), "helloworld");
        sslObject.wbio->writestr("[0005HELLOWORLD]");
        return len;
    });

    retval = funcTable->EncryptMessage(&sspCtx, 0, &dataBufDesc, 0);
    ASSERT_EQ(retval, SEC_E_OK);
    ASSERT_EQ(dataBuf[0], "[0005");
    ASSERT_EQ(dataBuf[1], "HELLOWORLD");
    ASSERT_EQ(dataBuf[2], "]");

    EXPECT_CALL(openssl, SSL_free(&sslObject));
    funcTable->DeleteSecurityContext(&sspCtx);
}
