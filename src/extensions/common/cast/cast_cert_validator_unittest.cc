// Copyright 2016 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "extensions/common/cast/cast_cert_validator.h"

#include "extensions/common/cast/cast_cert_validator_test_helpers.h"
#include "testing/gtest/include/gtest/gtest.h"

namespace extensions {

namespace api {

namespace cast_crypto {

namespace {

// Creates an std::string given a uint8_t array.
template <size_t N>
std::string CreateString(const uint8_t (&data)[N]) {
  return std::string(reinterpret_cast<const char*>(data), N);
}

// Indicates the expected result of test verification.
enum TestResult {
  RESULT_SUCCESS,
  RESULT_FAIL,
};

// Reads a test chain from |certs_file_name|, and asserts that verifying it as
// a Cast device certificate yields |expected_result|.
//
// RunTest() also checks that the resulting CertVerificationContext does not
// incorrectly verify invalid signatures.
//
//  * |expected_policy| - The policy that should have been identified for the
//                        device certificate.
//  * |time| - The timestamp to use when verifying the certificate.
//  * |optional_signed_data_file_name| - optional path to a PEM file containing
//        a valid signature generated by the device certificate.
//
void RunTest(TestResult expected_result,
             const std::string& expected_common_name,
             CastDeviceCertPolicy expected_policy,
             const std::string& certs_file_name,
             const base::Time::Exploded& time,
             const std::string& optional_signed_data_file_name) {
  auto certs = cast_test_helpers::ReadCertificateChainFromFile(certs_file_name);

  scoped_ptr<CertVerificationContext> context;
  CastDeviceCertPolicy policy;
  bool result = VerifyDeviceCert(certs, time, &context, &policy);

  if (expected_result == RESULT_FAIL) {
    ASSERT_FALSE(result);
    return;
  }

  ASSERT_TRUE(result);
  EXPECT_EQ(expected_policy, policy);
  ASSERT_TRUE(context.get());

  // Test that the context is good.
  EXPECT_EQ(expected_common_name, context->GetCommonName());

  ASSERT_TRUE(context);

  // Test verification of some invalid signatures.
  EXPECT_FALSE(
      context->VerifySignatureOverData("bogus signature", "bogus data"));
  EXPECT_FALSE(context->VerifySignatureOverData("", "bogus data"));
  EXPECT_FALSE(context->VerifySignatureOverData("", ""));

  // If valid signatures are known for this device certificate, test them.
  if (!optional_signed_data_file_name.empty()) {
    auto signature_data = cast_test_helpers::ReadSignatureTestData(
        optional_signed_data_file_name);

    // Test verification of a valid SHA1 signature.
    EXPECT_TRUE(context->VerifySignatureOverData(signature_data.signature_sha1,
                                                 signature_data.message));

    // Test verification of a valid SHA256
    //
    // TODO(eroman): This fails because there isn't currently support
    // for specifying a signature algorithm other than RSASSA PKCS#1 v1.5 with
    // SHA1. Once support for different algorithms is added to the API this
    // should be changed to expect success.
    EXPECT_FALSE(context->VerifySignatureOverData(
        signature_data.signature_sha256, signature_data.message));
  }
}

// Creates a time in UTC at midnight.
base::Time::Exploded CreateDate(int year, int month, int day) {
  base::Time::Exploded time = {0};
  time.year = year;
  time.month = month;
  time.day_of_month = day;
  return time;
}

// Returns 2016-04-01 00:00:00 UTC.
//
// This is a time when most of the test certificate paths are
// valid.
base::Time::Exploded AprilFirst2016() {
  return CreateDate(2016, 4, 1);
}

// Returns 2015-01-01 00:00:00 UTC.
base::Time::Exploded JanuaryFirst2015() {
  return CreateDate(2015, 1, 1);
}

// Returns 2040-03-01 00:00:00 UTC.
//
// This is so far in the future that the test chains in this unit-test
// should all be invalid.
base::Time::Exploded MarchFirst2040() {
  return CreateDate(2040, 3, 1);
}

// Tests verifying a valid certificate chain of length 2:
//
//   0: 2ZZBG9 FA8FCA3EF91A
//   1: Eureka Gen1 ICA
//
// Chains to trust anchor:
//   Eureka Root CA    (not included)
TEST(VerifyCastDeviceCertTest, ChromecastGen1) {
  RunTest(RESULT_SUCCESS, "2ZZBG9 FA8FCA3EF91A", CastDeviceCertPolicy::NONE,
          "cast_certificates/chromecast_gen1.pem", AprilFirst2016(),
          "cast_signeddata/2ZZBG9_FA8FCA3EF91A.pem");
}

// Tests verifying a valid certificate chain of length 2:
//
//  0: 2ZZBG9 FA8FCA3EF91A
//  1: Eureka Gen1 ICA
//
// Chains to trust anchor:
//   Cast Root CA     (not included)
TEST(VerifyCastDeviceCertTest, ChromecastGen1Reissue) {
  RunTest(RESULT_SUCCESS, "2ZZBG9 FA8FCA3EF91A", CastDeviceCertPolicy::NONE,
          "cast_certificates/chromecast_gen1_reissue.pem", AprilFirst2016(),
          "cast_signeddata/2ZZBG9_FA8FCA3EF91A.pem");
}

// Tests verifying a valid certificate chain of length 2:
//
//   0: 3ZZAK6 FA8FCA3F0D35
//   1: Chromecast ICA 3
//
// Chains to trust anchor:
//   Cast Root CA     (not included)
TEST(VerifyCastDeviceCertTest, ChromecastGen2) {
  RunTest(RESULT_SUCCESS, "3ZZAK6 FA8FCA3F0D35", CastDeviceCertPolicy::NONE,
          "cast_certificates/chromecast_gen2.pem", AprilFirst2016(), "");
}

// Tests verifying a valid certificate chain of length 3:
//
//   0: -6394818897508095075
//   1: Asus fugu Cast ICA
//   2: Widevine Cast Subroot
//
// Chains to trust anchor:
//   Cast Root CA     (not included)
TEST(VerifyCastDeviceCertTest, Fugu) {
  RunTest(RESULT_SUCCESS, "-6394818897508095075", CastDeviceCertPolicy::NONE,
          "cast_certificates/fugu.pem", AprilFirst2016(), "");
}

// Tests verifying an invalid certificate chain of length 1:
//
//  0: Cast Test Untrusted Device
//
// Chains to:
//   Cast Test Untrusted ICA    (not included)
//
// This is invalid because it does not chain to a trust anchor.
TEST(VerifyCastDeviceCertTest, Unchained) {
  RunTest(RESULT_FAIL, "", CastDeviceCertPolicy::NONE,
          "cast_certificates/unchained.pem", AprilFirst2016(), "");
}

// Tests verifying one of the self-signed trust anchors (chain of length 1):
//
//  0: Cast Root CA
//
// Chains to trust anchor:
//   Cast Root CA
//
// Although this is a valid and trusted certificate (it is one of the
// trust anchors after all) it fails the test as it is not a *device
// certificate*.
TEST(VerifyCastDeviceCertTest, CastRootCa) {
  RunTest(RESULT_FAIL, "", CastDeviceCertPolicy::NONE,
          "cast_certificates/cast_root_ca.pem", AprilFirst2016(), "");
}

// Tests verifying a valid certificate chain of length 2:
//
//  0: 4ZZDZJ FA8FCA7EFE3C
//  1: Chromecast ICA 4 (Audio)
//
// Chains to trust anchor:
//   Cast Root CA     (not included)
//
// This device certificate has a policy that means it is valid only for audio
// devices.
TEST(VerifyCastDeviceCertTest, ChromecastAudio) {
  RunTest(RESULT_SUCCESS, "4ZZDZJ FA8FCA7EFE3C",
          CastDeviceCertPolicy::AUDIO_ONLY,
          "cast_certificates/chromecast_audio.pem", AprilFirst2016(), "");
}

// Tests verifying a valid certificate chain of length 3:
//
//  0: MediaTek Audio Dev Test
//  1: MediaTek Audio Dev Model
//  2: Cast Audio Dev Root CA
//
// Chains to trust anchor:
//   Cast Root CA     (not included)
//
// This device certificate has a policy that means it is valid only for audio
// devices.
TEST(VerifyCastDeviceCertTest, MtkAudioDev) {
  RunTest(RESULT_SUCCESS, "MediaTek Audio Dev Test",
          CastDeviceCertPolicy::AUDIO_ONLY,
          "cast_certificates/mtk_audio_dev.pem", JanuaryFirst2015(), "");
}

// Tests verifying a valid certificate chain of length 2:
//
//  0: 9V0000VB FA8FCA784D01
//  1: Cast TV ICA (Vizio)
//
// Chains to trust anchor:
//   Cast Root CA     (not included)
TEST(VerifyCastDeviceCertTest, Vizio) {
  RunTest(RESULT_SUCCESS, "9V0000VB FA8FCA784D01", CastDeviceCertPolicy::NONE,
          "cast_certificates/vizio.pem", AprilFirst2016(), "");
}

// Tests verifying a valid certificate chain of length 2 using expired
// time points.
TEST(VerifyCastDeviceCertTest, ChromecastGen2InvalidTime) {
  const char* kCertsFile = "cast_certificates/chromecast_gen2.pem";

  // Control test - certificate should be valid at some time otherwise
  // this test is pointless.
  RunTest(RESULT_SUCCESS, "3ZZAK6 FA8FCA3F0D35", CastDeviceCertPolicy::NONE,
          kCertsFile, AprilFirst2016(), "");

  // Use a time before notBefore.
  RunTest(RESULT_FAIL, "", CastDeviceCertPolicy::NONE, kCertsFile,
          JanuaryFirst2015(), "");

  // Use a time after notAfter.
  RunTest(RESULT_FAIL, "", CastDeviceCertPolicy::NONE, kCertsFile,
          MarchFirst2040(), "");
}

// Tests verifying a valid certificate chain of length 3:
//
//  0: Audio Reference Dev Test
//  1: Audio Reference Dev Model
//  2: Cast Audio Dev Root CA
//
// Chains to trust anchor:
//   Cast Root CA     (not included)
//
// This device certificate has a policy that means it is valid only for audio
// devices.
TEST(VerifyCastDeviceCertTest, AudioRefDevTestChain3) {
  RunTest(RESULT_SUCCESS, "Audio Reference Dev Test",
          CastDeviceCertPolicy::AUDIO_ONLY,
          "cast_certificates/audio_ref_dev_test_chain_3.pem", AprilFirst2016(),
          "cast_signeddata/AudioReferenceDevTest.pem");
}

// ------------------------------------------------------
// Valid signature using 1024-bit RSA key
// ------------------------------------------------------

// This test vector comes from the NIST test vectors (pkcs1v15sign-vectors.txt),
// PKCS#1 v1.5 Signature Example 1.2.
//
// It is a valid signature using a 1024 bit key and SHA-1.

const uint8_t kEx1Message[] = {
    0x85, 0x13, 0x84, 0xcd, 0xfe, 0x81, 0x9c, 0x22, 0xed, 0x6c, 0x4c,
    0xcb, 0x30, 0xda, 0xeb, 0x5c, 0xf0, 0x59, 0xbc, 0x8e, 0x11, 0x66,
    0xb7, 0xe3, 0x53, 0x0c, 0x4c, 0x23, 0x3e, 0x2b, 0x5f, 0x8f, 0x71,
    0xa1, 0xcc, 0xa5, 0x82, 0xd4, 0x3e, 0xcc, 0x72, 0xb1, 0xbc, 0xa1,
    0x6d, 0xfc, 0x70, 0x13, 0x22, 0x6b, 0x9e,
};

const uint8_t kEx1Signature[] = {
    0x84, 0xfd, 0x2c, 0xe7, 0x34, 0xec, 0x1d, 0xa8, 0x28, 0xd0, 0xf1, 0x5b,
    0xf4, 0x9a, 0x87, 0x07, 0xc1, 0x5d, 0x05, 0x94, 0x81, 0x36, 0xde, 0x53,
    0x7a, 0x3d, 0xb4, 0x21, 0x38, 0x41, 0x67, 0xc8, 0x6f, 0xae, 0x02, 0x25,
    0x87, 0xee, 0x9e, 0x13, 0x7d, 0xae, 0xe7, 0x54, 0x73, 0x82, 0x62, 0x93,
    0x2d, 0x27, 0x1c, 0x74, 0x4c, 0x6d, 0x3a, 0x18, 0x9a, 0xd4, 0x31, 0x1b,
    0xdb, 0x02, 0x04, 0x92, 0xe3, 0x22, 0xfb, 0xdd, 0xc4, 0x04, 0x06, 0xea,
    0x86, 0x0d, 0x4e, 0x8e, 0xa2, 0xa4, 0x08, 0x4a, 0xa9, 0x8b, 0x96, 0x22,
    0xa4, 0x46, 0x75, 0x6f, 0xdb, 0x74, 0x0d, 0xdb, 0x3d, 0x91, 0xdb, 0x76,
    0x70, 0xe2, 0x11, 0x66, 0x1b, 0xbf, 0x87, 0x09, 0xb1, 0x1c, 0x08, 0xa7,
    0x07, 0x71, 0x42, 0x2d, 0x1a, 0x12, 0xde, 0xf2, 0x9f, 0x06, 0x88, 0xa1,
    0x92, 0xae, 0xbd, 0x89, 0xe0, 0xf8, 0x96, 0xf8,
};

const uint8_t kEx1PublicKeySpki[] = {
    0x30, 0x81, 0x9f, 0x30, 0x0d, 0x06, 0x09, 0x2a, 0x86, 0x48, 0x86, 0xf7,
    0x0d, 0x01, 0x01, 0x01, 0x05, 0x00, 0x03, 0x81, 0x8d, 0x00, 0x30, 0x81,
    0x89, 0x02, 0x81, 0x81, 0x00, 0xa5, 0x6e, 0x4a, 0x0e, 0x70, 0x10, 0x17,
    0x58, 0x9a, 0x51, 0x87, 0xdc, 0x7e, 0xa8, 0x41, 0xd1, 0x56, 0xf2, 0xec,
    0x0e, 0x36, 0xad, 0x52, 0xa4, 0x4d, 0xfe, 0xb1, 0xe6, 0x1f, 0x7a, 0xd9,
    0x91, 0xd8, 0xc5, 0x10, 0x56, 0xff, 0xed, 0xb1, 0x62, 0xb4, 0xc0, 0xf2,
    0x83, 0xa1, 0x2a, 0x88, 0xa3, 0x94, 0xdf, 0xf5, 0x26, 0xab, 0x72, 0x91,
    0xcb, 0xb3, 0x07, 0xce, 0xab, 0xfc, 0xe0, 0xb1, 0xdf, 0xd5, 0xcd, 0x95,
    0x08, 0x09, 0x6d, 0x5b, 0x2b, 0x8b, 0x6d, 0xf5, 0xd6, 0x71, 0xef, 0x63,
    0x77, 0xc0, 0x92, 0x1c, 0xb2, 0x3c, 0x27, 0x0a, 0x70, 0xe2, 0x59, 0x8e,
    0x6f, 0xf8, 0x9d, 0x19, 0xf1, 0x05, 0xac, 0xc2, 0xd3, 0xf0, 0xcb, 0x35,
    0xf2, 0x92, 0x80, 0xe1, 0x38, 0x6b, 0x6f, 0x64, 0xc4, 0xef, 0x22, 0xe1,
    0xe1, 0xf2, 0x0d, 0x0c, 0xe8, 0xcf, 0xfb, 0x22, 0x49, 0xbd, 0x9a, 0x21,
    0x37, 0x02, 0x03, 0x01, 0x00, 0x01,
};

// Tests that a valid signature fails, because it uses a 1024-bit RSA key (too
// weak).
TEST(VerifyCastDeviceCertTest, VerifySignature1024BitRsa) {
  auto context =
      CertVerificationContextImplForTest(CreateString(kEx1PublicKeySpki));

  EXPECT_FALSE(context->VerifySignatureOverData(CreateString(kEx1Signature),
                                                CreateString(kEx1Message)));
}

// ------------------------------------------------------
// Valid signature using 2048-bit RSA key
// ------------------------------------------------------

// This test vector was generated (using WebCrypto). It is a valid signature
// using a 2048-bit RSA key, RSASSA PKCS#1 v1.5 with SHA-1.

const uint8_t kEx2Message[] = {
    // "hello"
    0x68, 0x65, 0x6c, 0x6c, 0x6f,
};

const uint8_t kEx2Signature[] = {
    0xc1, 0x21, 0x84, 0xe1, 0x62, 0x0e, 0x59, 0x52, 0x5b, 0xa4, 0x10, 0x1e,
    0x11, 0x80, 0x5b, 0x9e, 0xcb, 0xa0, 0x20, 0x78, 0x29, 0xfc, 0xc0, 0x9a,
    0xd9, 0x48, 0x90, 0x81, 0x03, 0xa9, 0xc0, 0x2f, 0x0a, 0xc4, 0x20, 0x34,
    0xb5, 0xdb, 0x19, 0x04, 0xec, 0x94, 0x9b, 0xba, 0x48, 0x43, 0xf3, 0x5a,
    0x15, 0x56, 0xfc, 0x4a, 0x87, 0x79, 0xf8, 0x50, 0xff, 0x5d, 0x66, 0x25,
    0xdc, 0xa5, 0xd8, 0xe8, 0x9f, 0x5a, 0x73, 0x79, 0x6f, 0x5d, 0x99, 0xe0,
    0xd5, 0xa5, 0x84, 0x49, 0x20, 0x3c, 0xe2, 0xa3, 0xd0, 0x69, 0x31, 0x2c,
    0x13, 0xaf, 0x15, 0xd9, 0x10, 0x0d, 0x6f, 0xdd, 0x9d, 0x62, 0x5d, 0x7b,
    0xe1, 0x1a, 0x48, 0x59, 0xaf, 0xf7, 0xbe, 0x87, 0x92, 0x60, 0x5d, 0x1a,
    0xb5, 0xfe, 0x27, 0x38, 0x02, 0x20, 0xe9, 0xaf, 0x04, 0x57, 0xd3, 0x3b,
    0x70, 0x04, 0x63, 0x5b, 0xc6, 0x5d, 0x83, 0xe2, 0xaf, 0x02, 0xb4, 0xef,
    0x1c, 0x33, 0x54, 0x38, 0xf8, 0xb5, 0x19, 0xa8, 0x88, 0xdd, 0x1d, 0x96,
    0x1c, 0x5e, 0x54, 0x80, 0xde, 0x7b, 0xb6, 0x29, 0xb8, 0x6b, 0xea, 0x47,
    0xe5, 0xf1, 0x7e, 0xed, 0xe1, 0x91, 0xc8, 0xb8, 0x54, 0xd9, 0x1e, 0xfd,
    0x07, 0x10, 0xbd, 0xa9, 0xd4, 0x93, 0x5e, 0x65, 0x8b, 0x6b, 0x46, 0x93,
    0x4b, 0x60, 0x2a, 0x26, 0xf0, 0x1b, 0x4e, 0xca, 0x04, 0x82, 0xc0, 0x8d,
    0xb1, 0xa5, 0xa8, 0x70, 0xdd, 0x66, 0x68, 0x95, 0x09, 0xb4, 0x85, 0x62,
    0xf5, 0x17, 0x04, 0x48, 0xb4, 0x9d, 0x66, 0x2b, 0x25, 0x82, 0x7e, 0x99,
    0x3e, 0xa1, 0x11, 0x63, 0xc3, 0xdf, 0x10, 0x20, 0x52, 0x56, 0x32, 0x35,
    0xa9, 0x36, 0xde, 0x2a, 0xac, 0x10, 0x0d, 0x75, 0x21, 0xed, 0x5b, 0x38,
    0xb6, 0xb5, 0x1e, 0xb5, 0x5b, 0x9a, 0x72, 0xd5, 0xf8, 0x1a, 0xd3, 0x91,
    0xb8, 0x29, 0x0e, 0x58,
};

const uint8_t kEx2PublicKeySpki[] = {
    0x30, 0x82, 0x01, 0x22, 0x30, 0x0d, 0x06, 0x09, 0x2a, 0x86, 0x48, 0x86,
    0xf7, 0x0d, 0x01, 0x01, 0x01, 0x05, 0x00, 0x03, 0x82, 0x01, 0x0f, 0x00,
    0x30, 0x82, 0x01, 0x0a, 0x02, 0x82, 0x01, 0x01, 0x00, 0xcf, 0xde, 0xa5,
    0x2e, 0x9d, 0x38, 0x62, 0x72, 0x47, 0x84, 0x8f, 0x2e, 0xa5, 0xe3, 0xd6,
    0x34, 0xb0, 0xf9, 0x79, 0xa9, 0x10, 0x63, 0xa9, 0x93, 0x5a, 0xa1, 0xb9,
    0xa3, 0x03, 0xd3, 0xcd, 0x9d, 0x84, 0x7d, 0xb6, 0x92, 0x47, 0xb4, 0x7d,
    0x4a, 0xe8, 0x3a, 0x4b, 0xc5, 0xf6, 0x35, 0x6f, 0x18, 0x72, 0xf3, 0xbc,
    0xd2, 0x1c, 0x7a, 0xd2, 0xe5, 0xdf, 0xcf, 0xb9, 0xac, 0x28, 0xd3, 0x49,
    0x2a, 0x4f, 0x08, 0x62, 0xb9, 0xf1, 0xaa, 0x3d, 0x76, 0xe3, 0xa9, 0x96,
    0x32, 0x24, 0x94, 0x9e, 0x88, 0xf8, 0x5e, 0xc3, 0x3c, 0x14, 0x32, 0x86,
    0x72, 0xa2, 0x34, 0x3d, 0x41, 0xd0, 0xb2, 0x01, 0x99, 0x01, 0xf3, 0x93,
    0xa3, 0x76, 0x5a, 0xff, 0x42, 0x28, 0x54, 0xe0, 0xcc, 0x4c, 0xcd, 0x2d,
    0x3b, 0x0b, 0x47, 0xcc, 0xc2, 0x75, 0x02, 0xc1, 0xb7, 0x0b, 0x37, 0x65,
    0xe6, 0x0d, 0xe4, 0xc3, 0x85, 0x86, 0x29, 0x3c, 0x77, 0xce, 0xb0, 0x34,
    0xa9, 0x03, 0xe9, 0x13, 0xbe, 0x97, 0x1e, 0xfd, 0xeb, 0x0d, 0x60, 0xc2,
    0xb3, 0x19, 0xa1, 0x75, 0x72, 0x57, 0x3f, 0x5d, 0x0e, 0x75, 0xac, 0x10,
    0x96, 0xad, 0x95, 0x67, 0x9f, 0xa2, 0x84, 0x15, 0x6a, 0x61, 0xb1, 0x47,
    0xd1, 0x24, 0x78, 0xb4, 0x40, 0x2b, 0xc3, 0x5c, 0x73, 0xd4, 0xc1, 0x8d,
    0x12, 0xf1, 0x3f, 0xb4, 0x93, 0x17, 0xfe, 0x5d, 0xbf, 0x39, 0xf2, 0x45,
    0xf9, 0xcf, 0x38, 0x44, 0x40, 0x5b, 0x47, 0x2a, 0xbf, 0xb9, 0xac, 0xa6,
    0x14, 0xb6, 0x1b, 0xe3, 0xa8, 0x14, 0xf8, 0xfe, 0x47, 0x67, 0xea, 0x90,
    0x51, 0x12, 0xcf, 0x5e, 0x28, 0xec, 0x92, 0x83, 0x7c, 0xc6, 0x29, 0x9f,
    0x12, 0x29, 0x88, 0x49, 0xf7, 0xb7, 0xed, 0x5e, 0x3a, 0x78, 0xd6, 0x8a,
    0xba, 0x42, 0x6e, 0x0a, 0xf4, 0x0d, 0xc1, 0xc0, 0x8f, 0xdb, 0x26, 0x41,
    0x57, 0x02, 0x03, 0x01, 0x00, 0x01,
};

// Tests that a valid signature using 2048-bit key succeeds.
TEST(VerifyCastDeviceCertTest, VerifySignature2048BitRsa) {
  auto context =
      CertVerificationContextImplForTest(CreateString(kEx2PublicKeySpki));

  EXPECT_TRUE(context->VerifySignatureOverData(CreateString(kEx2Signature),
                                               CreateString(kEx2Message)));
}

}  // namespace

}  // namespace cast_crypto

}  // namespace api

}  // namespace extensions
