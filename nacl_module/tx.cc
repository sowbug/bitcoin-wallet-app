#include "tx.h"

#include <iostream>  // cerr
#include <iterator>
#include <set>
#include <sstream>
#include <vector>

#include "base58.h"
#include "node.h"
#include "node_factory.h"

bytes_t UnspentTxo::GetSigningAddress() {
  if (script.size() == 25 &&
      script[0] == 0x76 && script[1] == 0xa9 && script[2] == 0x14 &&
      script[23] == 0x88 && script[24] == 0xac) {
    // Standard Pay-to-PubkeyHash.
    // https://en.bitcoin.it/wiki/Transactions
    return bytes_t(script.begin() + 3, script.begin() + 3 + 20);
  }
  return bytes_t();
}

TxOut::TxOut(const bytes_t& hash_, uint64_t value_) :
  hash(hash_), value(value_) {}

// static
bool Tx::CreateSignedTransaction(const Node& sending_node,
                                 const unspent_txos_t& unspent_txos,
                                 const bytes_t& recipient_hash160,
                                 uint64_t value,
                                 uint64_t fee,
                                 uint32_t change_index,
                                 bytes_t& signed_tx) {
  // running total = fee
  // for each unspent_txo
  //   confirm we have the private key for this txo
  //     exit if not
  //   add that key to list of signers
  //   add the txo to list of inputs
  //   add balance to running total
  //   break when we have reached balance
  // exit if we ran out of funds
  // serialize transaction
  // sign transaction

  uint64_t change_value = 0;

  // Identify enough unspent_txos to cover transaction value.
  uint64_t required_value = fee + value;
  unspent_txos_t required_unspent_txos;
  for (unspent_txos_t::const_reverse_iterator i = unspent_txos.rbegin();
       i != unspent_txos.rend();
       ++i) {
    if (required_value == 0) {
      break;
    }
    required_unspent_txos.push_back(*i);
    if (required_value >= i->value) {
      required_value -= i->value;
    } else {
      change_value = i->value - required_value;
      required_value = 0;
    }
  }
  if (required_value != 0) {
    // Not enough funds to cover transaction.
    std::cerr << "Not enough funds" << std::endl;
    return false;
  }

  // We know which unspent_txos we intend to use. Create a set of
  // required addresses. Note that an address here is the hash160,
  // because that's the format embedded in the script.
  std::set<bytes_t> required_signing_addresses;
  for (unspent_txos_t::reverse_iterator i = required_unspent_txos.rbegin();
       i != required_unspent_txos.rend();
       ++i) {
    required_signing_addresses.insert(i->GetSigningAddress());
  }

  // Do we have all the keys for the required addresses? Generate
  // them. For now we're going to assume no account has more than 16
  // addresses, so that's the farthest we'll walk down the chain.
  std::set<bytes_t> signing_keys;
  uint32_t start = 0;
  uint32_t count = 16;
  for (uint32_t i = 0; i < count; ++i) {
    std::stringstream node_path;
    node_path << "m/" << (start + i);
    Node* node =
      NodeFactory::DeriveChildNodeWithPath(sending_node, node_path.str());
    bytes_t hash160 = Base58::toHash160(node->public_key());
    if (required_signing_addresses.find(hash160) !=
        required_signing_addresses.end()) {
      signing_keys.insert(node->secret_key());
    }
    delete node;
    if (signing_keys.size() == required_signing_addresses.size()) {
      break;
    }
  }
  if (signing_keys.size() != required_signing_addresses.size()) {
    // We don't have all the keys we need to spend these funds.
    std::cerr << "missing some keys" << std::endl;
    return false;
  }

  tx_outs_t recipients;
  TxOut txout(recipient_hash160, value);
  recipients.push_back(txout);

  if (change_value != 0) {
    // Derive the change address
    std::stringstream node_path;
    node_path << "m/" << (change_index);
    Node* node =
      NodeFactory::DeriveChildNodeWithPath(sending_node, node_path.str());
    bytes_t hash160 = Base58::toHash160(node->public_key());
    recipients.push_back(TxOut(hash160, change_value));
    delete node;
  }

  signed_tx.resize(0);

  // https://en.bitcoin.it/wiki/Transactions

  // Version 1
  signed_tx.push_back(1);
  signed_tx.push_back(0);
  signed_tx.push_back(0);
  signed_tx.push_back(0);

  // Number of inputs
  if (required_unspent_txos.size() >= 0xfd) {
    // TODO: implement varint
    std::cerr << "too many inputs" << std::endl;
    return false;
  }
  signed_tx.push_back(required_unspent_txos.size() & 0xff);

  // txin
  for (unspent_txos_t::const_iterator i = required_unspent_txos.begin();
       i != required_unspent_txos.end();
       ++i) {
    // For some stupid reason the hash is serialized backwards.
    for (bytes_t::const_reverse_iterator j = i->hash.rbegin();
         j != i->hash.rend();
         ++j) {
      signed_tx.push_back(*j);
    }

    // Previous TXO index
    signed_tx.push_back((i->output_n) & 0xff);
    signed_tx.push_back((i->output_n >> 8) & 0xff);
    signed_tx.push_back((i->output_n >> 16) & 0xff);
    signed_tx.push_back((i->output_n >> 24) & 0xff);

    // TODO ScriptSig
    signed_tx.push_back(0);

    // sequence_no
    signed_tx.push_back(0xff);
    signed_tx.push_back(0xff);
    signed_tx.push_back(0xff);
    signed_tx.push_back(0xff);
  }

  // Number of outputs
  if (recipients.size() >= 0xfd) {
    // TODO: implement varint
    std::cerr << "too many recipients" << std::endl;
    return false;
  }
  signed_tx.push_back(recipients.size() & 0xff);

  for (tx_outs_t::iterator i = recipients.begin();
       i != recipients.end();
       ++i) {
    uint64_t recipient_value = i->value;
    signed_tx.push_back((recipient_value) & 0xff);
    signed_tx.push_back((recipient_value >>  8) & 0xff);
    signed_tx.push_back((recipient_value >> 16) & 0xff);
    signed_tx.push_back((recipient_value >> 24) & 0xff);
    signed_tx.push_back((recipient_value >> 32) & 0xff);
    signed_tx.push_back((recipient_value >> 40) & 0xff);
    signed_tx.push_back((recipient_value >> 48) & 0xff);
    signed_tx.push_back((recipient_value >> 56) & 0xff);

    bytes_t script;
    script.push_back(0x76);  // OP_DUP
    script.push_back(0xa9);  // OP_HASH160
    script.push_back(0x14);  // 20 bytes, should probably assert == hashlen
    script.insert(script.end(), i->hash.begin(), i->hash.end());
    script.push_back(0x88);  // OP_EQUALVERIFY
    script.push_back(0xac);  // OP_CHECKSIG

    signed_tx.push_back(script.size() & 0xff);
    signed_tx.insert(signed_tx.end(), script.begin(), script.end());
  }

  // Lock time
  signed_tx.push_back(0);
  signed_tx.push_back(0);
  signed_tx.push_back(0);
  signed_tx.push_back(0);

  std::cerr << to_hex(signed_tx) << std::endl;

  return true;
}
