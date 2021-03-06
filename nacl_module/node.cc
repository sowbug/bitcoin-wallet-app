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

#include "node.h"

#include <sstream>
#include <string>

#include "base58.h"
#include "crypto.h"
#include "secp256k1.h"

Node::Node(const bytes_t& key,
           const bytes_t& chain_code,
           uint32_t version,
           unsigned int depth,
           uint32_t parent_fingerprint,
           uint32_t child_num) :
  version_(version),
  depth_(depth),
  parent_fingerprint_(parent_fingerprint),
  child_num_(child_num) {
  set_key(key);
  set_chain_code(chain_code);
}

Node::~Node() {
}

std::string Node::toString() const {
  std::stringstream ss;
  ss << "version: " << std::hex << version_ << std::endl
     << "hex_id: " << to_hex(hex_id_) << std::endl
     << "fingerprint: " << std::hex << fingerprint_ << std::endl
     << "secret_key: " << to_hex(secret_key_) << std::endl
     << "public_key: " << to_hex(public_key_) << std::endl
     << "chain_code: " << to_hex(chain_code_) << std::endl
     << "depth: " << depth_ << std::endl
     << "parent_fingerprint: " << std::hex << parent_fingerprint_ << std::endl
     << "child_num: " << child_num_ << std::endl
    ;

  return ss.str();
}

void Node::set_key(const bytes_t& new_key) {
  secret_key_.clear();
  // TODO(miket): check key_num validity
  is_private_ = new_key.size() == 32;
  version_ = is_private_ ? 0x0488ADE4 : 0x0488B21E;
  if (is_private()) {
    secret_key_ = new_key;
    secp256k1_key curvekey;
    curvekey.setPrivKey(secret_key_);
    public_key_ = curvekey.getPubKey();
  } else {
    public_key_ = new_key;
  }
  update_fingerprint();
}

void Node::set_chain_code(const bytes_t& new_code) {
  chain_code_ = new_code;
}

void Node::update_fingerprint() {
  hex_id_ = Crypto::SHA256ThenRIPE(public_key_);
  fingerprint_ = (uint32_t)hex_id_[0] << 24 |
    (uint32_t)hex_id_[1] << 16 |
    (uint32_t)hex_id_[2] << 8 |
    (uint32_t)hex_id_[3];
}

bytes_t Node::toSerialized(bool private_if_available) const {
  bytes_t s;
  bool should_generate_private = is_private() && private_if_available;

  // 4 byte: version bytes (mainnet: 0x0488B21E public, 0x0488ADE4 private;
  // testnet: 0x043587CF public, 0x04358394 private)
  uint32_t version = should_generate_private ? 0x0488ADE4 : 0x0488B21E;
  s.push_back((uint32_t)version >> 24);
  s.push_back(((uint32_t)version >> 16) & 0xff);
  s.push_back(((uint32_t)version >> 8) & 0xff);
  s.push_back((uint32_t)version & 0xff);

  // 1 byte: depth: 0x00 for master nodes, 0x01 for level-1 descendants, etc.
  s.push_back(depth_);

  // 4 bytes: the fingerprint of the parent's key (0x00000000 if master key)
  s.push_back((uint32_t)parent_fingerprint_ >> 24);
  s.push_back(((uint32_t)parent_fingerprint_ >> 16) & 0xff);
  s.push_back(((uint32_t)parent_fingerprint_ >> 8) & 0xff);
  s.push_back((uint32_t)parent_fingerprint_ & 0xff);

  // 4 bytes: child number. This is the number i in xi = xpar/i, with xi
  // the key being serialized. This is encoded in MSB order.
  // (0x00000000 if master key)
  s.push_back((uint32_t)child_num_ >> 24);
  s.push_back(((uint32_t)child_num_ >> 16) & 0xff);
  s.push_back(((uint32_t)child_num_ >> 8) & 0xff);
  s.push_back((uint32_t)child_num_ & 0xff);

  // 32 bytes: the chain code
  s.insert(s.end(), chain_code().begin(), chain_code().end());

  // 33 bytes: the public key or private key data (0x02 + X or 0x03 + X
  // for public keys, 0x00 + k for private keys)
  bool use_private = is_private() && private_if_available;
  const bytes_t& key = use_private ? secret_key() : public_key();
  if (use_private) {
    s.push_back(0x00);
  }
  s.insert(s.end(), key.begin(), key.end());

  return s;
}

bytes_t Node::toSerializedPublic() const {
  return toSerialized(false);
}

bytes_t Node::toSerializedPrivate() const {
  if (!is_private()) {
    return bytes_t();
  }
  return toSerialized(false);
}

bytes_t Node::toSerialized() const {
  return toSerialized(true);
}
