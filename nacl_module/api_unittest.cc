// Copyright 2014 Mike Tsao <mike@sowbug.com>

// Permission is hereby granted, free of charge, to any person
// obtaining a copy of this software and associated documentation
// files (the "Software"), to deal in the Software without
// restriction, including without limitation the rights to use, copy,
// modify, merge, publish, distribute, sublicense, and/or sell copies
// of the Software, and to permit persons to whom the Software is
// furnished to do so, subject to the following conditions:

// The above copyright notice and this permission notice shall be
// included in all copies or substantial portions of the Software.

// THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND,
// EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF
// MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND
// NONINFRINGEMENT. IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS
// BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN
// ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN
// CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
// SOFTWARE.

#include <iostream>
#include <fstream>
#include <string>

#include "api.h"
#include "gtest/gtest.h"
#include "jsoncpp/json/reader.h"
#include "jsoncpp/json/writer.h"
#include "types.h"

TEST(ApiTest, SendFunds) {
  Json::Value request;
  Json::Value response;
  Credentials c;
  Wallet w(c);
  API api(c, w);

  // Using parts of BIP 0032 Test Vector 1.
  //
  // - Root master key m is fingerprint 3442193e
  // - Sending account m/0' is fingerprint 5c1bd648
  // - unspent txo was sent to m/0'/0/0
  //   - L3dzheSvHWc2scJdiikdZmYdFzPcvZMAnT5g62ikVWZdBewoWpL1
  //   - 1BvgsfsZQVtkLS69NvGF8rw6NZW2ShJQHr
  //   - 77d896b0f85f72ae0f3d0487c432b23c28b71493
  // - recipient is m/1'/0/0
  //   - L51Rt2TvamJzvbBJz1UG27RMtfvPLwCKqFsXRYgL4EXRTjvMiYqM
  //   - 1AnDogBPp4VL48Nrh7h8LquV68ZzXNtwcq
  //   - 6b468a091d50dfb7557200c46d0c1999d060a637
  // - change address is m/0'/0/1 (we don't use the internal chain for now)
  //   - L22jhG8WTNmuRtqFvzvpnhe32F8FefJFfsLJpSr1CYsRrZCyTwKZ
  //   - 1B1TKfsCkW5LQ6R1kSXUx7hLt49m1kwz75
  //   - 6dc73af1c96ff68e9dbdecd7453bad59bf0c83a4
  Json::Value unspent_txos;
  unspent_txos[0]["tx_hash"] = "47b95fdeff3a20cb72d3ad499f0c34b2"
    "bdec16de51a3fcf95e5db57e9d61fb18";
  unspent_txos[0]["tx_output_n"] = 127;
  unspent_txos[0]["script"] =
    "76a914" \
    "77d896b0f85f72ae0f3d0487c432b23c28b71493" \
    "88ac";
  unspent_txos[0]["value"] = 100000000;  // 1 BTC

  Json::Value recipients;
  recipients[0] = Json::Value();
  recipients[0]["address"] = "1AnDogBPp4VL48Nrh7h8LquV68ZzXNtwcq";
  recipients[0]["value"] = 16383;

  request["ext_prv_b58"] = "xprv9uHRZZhk6KAJC1avXpDAp4MDc3sQKNxDiPvvkX8Br5ng"
    "LNv1TxvUxt4cV1rGL5hj6KCesnDYUhd7oWgT11eZG7XnxHrnYeSvkzY7d2bhkJ7";

  request["unspent_txos"] = unspent_txos;
  request["recipients"] = recipients;
  request["fee"] = 127;
  request["change_index"] = 1;

  //Json::StyledWriter writer;
  //std::cerr << writer.write(request) << std::endl;

  EXPECT_TRUE(api.HandleGetSignedTransaction(request, response));

  bytes_t signed_tx(unhexlify(response["signed_tx"].asString()));
  ASSERT_TRUE(signed_tx.size() > 64);

  // Validation is hard.

  // First byte of 32-bit version
  EXPECT_EQ(1, signed_tx[0]);

  // Last byte of 32-bit locktime
  EXPECT_EQ(0, signed_tx[0 + signed_tx.size() - 1]);
}

TEST(ApiTest, TransactionManager) {
  Credentials c;
  Wallet w(c);
  API api(c, w);
  Json::Value request;
  Json::Value response;

  // When we start out, there should be nothing.
  EXPECT_TRUE(api.HandleGetUnspentTxos(request, response));
  EXPECT_EQ(0, response["unspent_txos"].size());

  // We just received address history from the server. This sample is
  // an arbitrarily picked one that got included in a block moments
  // ago.
  request = Json::Value();
  response = Json::Value();
  request["address"] = "1DHhYn2hTgjuNkvGK1dMweiHX3R7eRsQKi";
  {
    // Two outputs, two unspent
    Json::Value history;
    history["tx_hash"] = "c357d77807368346fccc6e078bd28626a91d06f4a1ba8b891a455d23b53c9fef";
    history["height"] = 281363;
    request["history"].append(history);
  }
  {
    // Three outputs, two unspent
    Json::Value history;
    history["tx_hash"] = "f36357faeea4fc1f0e365d1af1be5071523ce63e987c34d35e51e2271d7e7276";
    history["height"] = 281359;
    request["history"].append(history);
  }
  // TM should respond that it knows nothing about these transactions.
  EXPECT_TRUE(api.HandleReportAddressHistory(request, response));
  EXPECT_EQ(2, response["unknown_txs"].size());

  // We ask the server for each of those transactions, and back they
  // come. Let's report them to TM.
  request = Json::Value();
  response = Json::Value();
  request["txs"][0] = "010000000176727e1d27e2515ed3347c983ee63c527150bef11a5d360e1ffca4eefa5763f3000000006a47304402207affb9e332bf8e0b606cd644abb5265deb67e9b9db2b24c270f663fb53226592022024e33bb5ea9a0a6e5fdc451740bebe7fdd4bc84d0362819df59e42c69a16219f012102b7fa5fce24461db7f4eca4590d99b89198c7e673b15856d88ce84925f12bf59cffffffff026cad7a29010000001976a91408bfbf564a1179feeeb021a7ea2fd48a3952fc1c88ac747aa813000000001976a9142d5aeacbdf114615533c16b7fe6309918ea49c8b88ac00000000";
  request["txs"][1] = "01000000012261e8c7b75726b79c204a678320e94373d0122bed8b19ac263a7f949f127c26010000006b48304502200d77784a48a350eac8c41b506eb81746a5e6f3522b100634f707a2fa5cf12cdb022100edb04c5de6017f4b621285ec7258100eb700d9cc6a68d1a74af131c87e530922012102e70b14967a4d0c752dbf59656ae3463e4780e2898031268341b97f54f2bd20c4ffffffff03c82b233d010000001976a91486ca032feb47d375e3c82d611d0d8b76632d6b7588ac1822ef08000000001976a9142ab266d8448c36c42dfaa3b2131b998fcc8578d788ac38326e18000000001976a9142ac74153f491a617a891f3f49db15ce6892e934688ac00000000";
  // TM should respond with a success message.
  EXPECT_TRUE(api.HandleReportTransactions(request, response));

  // Now we should have some unspent_txos.
  request = Json::Value();
  response = Json::Value();
  EXPECT_TRUE(api.HandleGetUnspentTxos(request, response));
  EXPECT_EQ(4, response["unspent_txos"].size());
}

TEST(ApiTest, HappyPath) {
  Credentials c;
  Wallet w(c);
  API api(c, w);
  Json::Value request;
  Json::Value response;

  request["new_passphrase"] = "foo";
  EXPECT_TRUE(api.HandleSetPassphrase(request, response));
  EXPECT_TRUE(api.DidResponseSucceed(response));

  // Import an xprv (see test/h9-test-vectors-script.txt)
  request = Json::Value();
  response = Json::Value();
  request["seed_hex"] = "baddecaf99887766554433221100";
  EXPECT_TRUE(api.HandleDeriveRootNode(request, response));
  EXPECT_TRUE(api.DidResponseSucceed(response));

  const std::string ext_pub_b58(response["ext_pub_b58"].asString());
  const std::string ext_prv_enc(response["ext_prv_enc"].asString());
  EXPECT_EQ("0x8bb9cbc0", response["fp"].asString());

  // Generate a root node and make sure the response changed
  request = Json::Value();
  response = Json::Value();
  EXPECT_TRUE(api.HandleGenerateRootNode(request, response));
  EXPECT_TRUE(api.DidResponseSucceed(response));
  EXPECT_NE(ext_pub_b58, response["ext_pub_b58"].asString());

  // Set back to the imported root node
  request = Json::Value();
  response = Json::Value();
  request["ext_pub_b58"] = ext_pub_b58;
  request["ext_prv_enc"] = ext_prv_enc;
  EXPECT_TRUE(api.HandleAddRootNode(request, response));
  EXPECT_TRUE(api.DidResponseSucceed(response));
  EXPECT_EQ("0x8bb9cbc0", response["fp"].asString());

  // Derive a child
  request = Json::Value();
  response = Json::Value();
  request["path"] = "m/0'";
  request["is_watch_only"] = false;
  //  request["public_addr_n"] = 2;
  //  request["change_addr_n"] = 2;
  EXPECT_TRUE(api.HandleDeriveChildNode(request, response));
  EXPECT_TRUE(api.DidResponseSucceed(response));
  EXPECT_EQ("0x5adb92c0", response["fp"].asString());
  EXPECT_EQ(8 + 8, response["address_statuses"].size());

  // TODO(miket): change to address_t, including value & is_public
  EXPECT_EQ("1KK55Nf8ZZ88jQzG5pwfEzwukyDvgFxKRy",  // m/0'/0/0
            response["address_statuses"][0]["address"].asString());
  EXPECT_EQ("1CbammCCGPPU4LX64xe33QcdjsYBWv4gHG",  // m/0'/1/0
            response["address_statuses"][8 + 0]["address"].asString());

  // Pretend we sent blockchain.address.get_history for each address
  // and got back some stuff.
  request = Json::Value();
  response = Json::Value();
  const std::string HASH_TX =
    "555ae5e6d83cd05975952e2725783ddd760076de3d918f9c33ef6895e99b363a";
  request["tx_statuses"][0]["hash"] = HASH_TX;
  request["tx_statuses"][0]["height"] = 282172;
  EXPECT_TRUE(api.HandleReportTxStatuses(request, response));
  EXPECT_TRUE(api.DidResponseSucceed(response));
  EXPECT_EQ(1, response["tx_requests"].size());
  EXPECT_EQ(HASH_TX, response["tx_requests"][0].asString());

  // Pretend we did a blockchain.transaction.get for the requested
  // transaction. We should get back an update to an address balance.
  request = Json::Value();
  response = Json::Value();
  request["txs"][0]["tx"] =
    "01000000019970765cdbceee5b6ab67491218f74a130aa6c81932d088c9b44ece1be7fbe1"
    "b010000006b483045022100cce48367450cc2a76e4033dd342b7792e7c36011bff6e71eef"
    "314a498045f09e02205a7fdbcb0d7428f8b3ca0818902727e9babb28f8a0582f5608f3a49"
    "c842d2e51012102ecbf6d557ccbf87295769deace203ee31fd3bb57813b38d1322881c38f"
    "30674dffffffff02400d0300000000001976a914c8dd2744f160f0f24537606b82e40d5d0"
    "815810388acb5941900000000001976a9147dcdbe519137c8ccdf54da3032b16b0005d79b"
    "4488ac00000000";
  EXPECT_TRUE(api.HandleReportTxs(request, response));
  EXPECT_TRUE(api.DidResponseSucceed(response));
  EXPECT_EQ(1, response["address_statuses"].size());
  EXPECT_EQ(200000, response["address_statuses"][0]["value"].asUInt64());

  // Spend some of the funds in the wallet.
  request = Json::Value();
  response = Json::Value();
  request["recipients"][0]["addr_b58"] = "1CUBwHRHD4D4ckRBu81n8cboGVUP9Ve7m4";
  request["recipients"][0]["value"] = 100000;
  request["fee"] = 0;
  request["change_index"] = 0;
  request["sign"] = true;

  EXPECT_TRUE(api.HandleCreateTx(request, response));

  EXPECT_TRUE(api.DidResponseSucceed(response));
  EXPECT_TRUE(response["tx"].asString().size() > 0);
  const bytes_t tx(unhexlify(response["tx"].asString()));
  std::cerr << response["tx"].asString() << std::endl;

  // Broadcast, then report that we got the transaction. Expect new balance.
  request = Json::Value();
  response = Json::Value();
  request["txs"][0]["tx"] = to_hex(tx);
  EXPECT_TRUE(api.HandleReportTxs(request, response));
  EXPECT_TRUE(api.DidResponseSucceed(response));
  EXPECT_EQ(1, response["address_statuses"].size());
  EXPECT_EQ(200000 - 100000 - 0,
            response["address_statuses"][0]["value"].asUInt64());
}

TEST(ApiTest, BadInput) {
  Credentials c;
  Wallet w(c);
  API api(c, w);
  Json::Value request;
  Json::Value response;

  request["new_passphrase"] = "foo";
  EXPECT_TRUE(api.HandleSetPassphrase(request, response));
  EXPECT_TRUE(api.DidResponseSucceed(response));

  // Import a bad xprv; should fail
  request = Json::Value();
  response = Json::Value();
  request["ext_prv_b58"] = "xprv9s21ZrQH143K3QTDL4LXw2F7HEK3wJUD2nW2nRk4stb"
    "Py6cq3jPPqjiChkVvvNKmPGJxWUtg6LnF5kejMRNNU3TGtRBeJgk33yuGBxrMPHz";
  EXPECT_TRUE(api.HandleImportRootNode(request, response));
  EXPECT_FALSE(api.DidResponseSucceed(response));
}
