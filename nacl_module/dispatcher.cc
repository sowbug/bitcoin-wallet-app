#include "node.h"

#include <iomanip>
#include <sstream>
#include <stdint.h>

#include "base58.h"
#include "crypto.h"
#include "json/reader.h"
#include "json/writer.h"
#include "node_factory.h"
#include "ppapi/cpp/instance.h"
#include "ppapi/cpp/module.h"
#include "ppapi/cpp/var.h"
#include "types.h"

class HDWalletDispatcherInstance : public pp::Instance {
public:
  explicit HDWalletDispatcherInstance(PP_Instance instance)
  : pp::Instance(instance) {}
  virtual ~HDWalletDispatcherInstance() {}

  void PopulateDictionaryFromNode(Json::Value& dict, Node* node) {
    dict["hex_id"] = to_hex(node->hex_id());
    dict["fingerprint"] = "0x" + to_fingerprint(node->fingerprint());
    dict["address"] = Base58::toAddress(node->public_key());
    dict["public_key"] = to_hex(node->public_key());
    dict["chain_code"] = to_hex(node->chain_code());
    dict["ext_pub_hex"] = to_hex(node->toSerializedPublic());
    dict["ext_pub_b58"] = Base58::toBase58Check(node->toSerializedPublic());
    if (node->is_private()) {
      dict["secret_key"] = to_hex(node->secret_key());
      dict["secret_wif"] = Base58::toPrivateKey(node->secret_key());
      dict["ext_prv_hex"] = to_hex(node->toSerialized());
      dict["ext_prv_b58"] = Base58::toBase58Check(node->toSerialized());
    }
  }

  virtual bool HandleGetNode(const Json::Value& args, Json::Value& result) {
    const std::string seed = args.get("seed", "").asString();
    const bytes_t seed_bytes(unhexlify(seed));

    Node *parent_node = NULL;
    if (seed_bytes.size() == 78) {
      parent_node = NodeFactory::CreateNodeFromExtended(seed_bytes);
    } else if (seed[0] == 'x') {
      parent_node =
        NodeFactory::CreateNodeFromExtended(Base58::fromBase58Check(seed));
    } else {
      parent_node = NodeFactory::CreateNodeFromSeed(seed_bytes);
    }

    const std::string node_path = args.get("path", "m").asString();
    Node* node =
      NodeFactory::DeriveChildNodeWithPath(*parent_node, node_path);
    delete parent_node;

    PopulateDictionaryFromNode(result, node);
    delete node;

    return true;
  }

  virtual bool HandleCreateNode(const Json::Value& /*args*/,
                                Json::Value& result) {
    bytes_t seed_bytes(32, 0);

    if (!Crypto::GetRandomBytes(seed_bytes)) {
      result["error_code"] = -1;
      result["error_message"] =
        std::string("The PRNG has not been seeded with enough "
                    "randomness to ensure an unpredictable byte sequence.");
      return true;
    }

    Node *node = NodeFactory::CreateNodeFromSeed(seed_bytes);
    PopulateDictionaryFromNode(result, node);
    delete node;
    return true;
  }

  virtual bool HandleGetAddresses(const Json::Value& args,
                                  Json::Value& result) {
    const std::string seed = args.get("seed", "").asString();
    const bytes_t seed_bytes(unhexlify(seed));

    Node *parent_node = NULL;
    if (seed_bytes.size() == 78) {
      parent_node = NodeFactory::CreateNodeFromExtended(seed_bytes);
    } else if (seed[0] == 'x') {
      parent_node =
        NodeFactory::CreateNodeFromExtended(Base58::fromBase58Check(seed));
    } else {
      parent_node = NodeFactory::CreateNodeFromSeed(seed_bytes);
    }

    uint32_t start = args.get("start", 0).asUInt();
    uint32_t count = args.get("count", 20).asUInt();
    const std::string base_node_path = args.get("path", "m").asString();
    for (uint32_t i = 0; i < count; ++i) {
      std::stringstream node_path;
      node_path << base_node_path << "/" << (start + i);
      Node* node =
        NodeFactory::DeriveChildNodeWithPath(*parent_node, node_path.str());
      result["addresses"][i]["index"] = i + start;
      result["addresses"][i]["path"] = node_path.str();
      result["addresses"][i]["address"] =
        Base58::toAddress(node->public_key());
      if (node->is_private()) {
        result["addresses"][i]["key"] =
          Base58::toPrivateKey(node->secret_key());
      }
      delete node;
    }
    delete parent_node;

    return true;
  }

  virtual bool VerifyCredentials(const bytes_t& key,
                                 const bytes_t& check,
                                 const bytes_t& internal_key_encrypted,
                                 bytes_t& internal_key,
                                 int& error_code,
                                 std::string& error_message) {
    bytes_t check_decrypted;
    if (!Crypto::Decrypt(key, check, check_decrypted)) {
      error_code = -2;
      error_message = "Check decryption failed";
      return false;
    }
    if (check_decrypted != unhexlify(PASSPHRASE_CHECK_HEX)) {
      error_code = -3;
      error_message = "Check verification failed";
      return false;
    }
    bytes_t internal_key;
    if (!Crypto::Decrypt(key, internal_key_encrypted, internal_key)) {
      error_code = -4;
      error_message = "internal_key decryption failed";
      return false;
    }
    return true;
  }

  virtual bool HandleSetPassphrase(const Json::Value& args,
                                   Json::Value& result) {
    bytes_t key(unhexlify(args("key", "").asString()));
    bytes_t check(unhexlify(args("check", "").asString()));
    bytes_t
      internal_key_encrypted(unhexlify(args.get("internal_key_encrypted", "")
                                       .asString()));
    const std::string new_passphrase = args["new_passphrase"].asString();

    bytes_t internal_key;
    if (key.size() > 0 &&
        check.size() > 0 &&
        internal_key_encrypted.size() > 0) {
      int error_code;
      std::string error_message;
      if (!VerifyCredentials(key,
                             check,
                             internal_key_encrypted,
                             internal_key,
                             error_code,
                             error_message)) {
        result["error_code"] = error_code;
        result["error_message"] = error_message;
        return true;
      }
    } else {
      internal_key.resize(32);
      Crypto::GetRandomBytes(internal_key);
    }
    key = bytes_t();
    check = bytes_t();

    bytes_t salt(32);
    Crypto::GetRandomBytes(salt);

    if (!Crypto::DeriveKey(new_passphrase, salt, key)) {
      result["error_code"] = -1;
      result["error_message"] = "Key derivation failed";
      return true;
    }
    if (!Crypto::Encrypt(key, unhexlify(PASSPHRASE_CHECK_HEX), check)) {
      result["error_code"] = -5;
      result["error_message"] = "Check generation failed";
      return true;
    }
    if (!Crypto::Encrypt(key, internal_key, internal_key_encrypted)) {
      result["error_code"] = -5;
      result["error_message"] = "Check generation failed";
      return true;
    }
    result["salt"] = to_hex(salt);
    result["key"] = to_hex(key);
    result["check"] = to_hex(check);
    result["internal_key"] = to_hex(internal_key);
    result["internal_key_encrypted"] = to_hex(internal_key_encrypted);
    return true;
  }

  virtual bool HandleUnlockWallet(const Json::Value& args,
                                  Json::Value& result) {
    const bytes_t salt(unhexlify(args["salt"].asString()));
    const bytes_t check(unhexlify(args["check"].asString()));
    const std::string passphrase = args["passphrase"].asString();
    const bytes_t
      internal_key_encrypted(unhexlify(args["internal_key_encrypted"]
                                       .asString()));

    bytes_t key(32, 0);
    if (!Crypto::DeriveKey(passphrase, salt, key)) {
      result["error_code"] = -1;
      result["error_message"] = "Key derivation failed";
      return true;
    }
    bytes_t internal_key;
    int error_code;
    std::string error_message;
    if (!VerifyCredentials(key,
                           check,
                           internal_key_encrypted,
                           internal_key,
                           error_code,
                           error_message)) {
      result["error_code"] = error_code;
      result["error_message"] = error_message;
      return true;
    }
    result["key"] = to_hex(key);
    result["internal_key"] = to_hex(internal_key);
    return true;
  }

  virtual bool HandleEncryptItem(const Json::Value& args,
                                 Json::Value& result) {
    bytes_t internal_key(unhexlify(args["internal_key"].asString()));
    const std::string item = args["item"].asString();
    bytes_t item_encrypted;
    bytes_t item_bytes(&item[0], &item[item.size()]);
    if (Crypto::Encrypt(internal_key, item_bytes, item_encrypted)) {
      result["item_encrypted"] = to_hex(item_encrypted);
    } else {
      result["error_code"] = -1;
    }
    return true;
  }

  virtual bool HandleDecryptItem(const Json::Value& args,
                                 Json::Value& result) {
    bytes_t internal_key(unhexlify(args["internal_key"].asString()));
    bytes_t item_encrypted(unhexlify(args["item_encrypted"].asString()));
    bytes_t item_bytes;
    if (Crypto::Decrypt(internal_key, item_encrypted, item_bytes)) {
      result["item"] =
        std::string(reinterpret_cast<char const*>(&item_bytes[0]),
                    item_bytes.size());
    } else {
      result["error_code"] = -1;
    }
    return true;
  }

  /// Handler for messages coming in from the browser via
  /// postMessage().  The @a var_message can contain be any pp:Var
  /// type; for example int, string Array or Dictionary. Please see
  /// the pp:Var documentation for more details.  @param[in]
  /// var_message The message posted by the browser.
  virtual void HandleMessage(const pp::Var& var_message) {
    if (!var_message.is_string())
      return;
    std::string message = var_message.AsString();
    Json::Value root;
    Json::Reader reader;
    bool parsingSuccessful = reader.parse(message, root);
    if (!parsingSuccessful) {
      //                 << reader.getFormattedErrorMessages();
      return;
    }
    const std::string command = root.get("command", "UTF-8").asString();
    Json::Value result;
    bool handled = false;
    if (command == "create-node") {
      handled = HandleCreateNode(root, result);
    }
    if (command == "get-node") {
      handled = HandleGetNode(root, result);
    }
    if (command == "get-addresses") {
      handled = HandleGetAddresses(root, result);
    }
    if (command == "set-passphrase") {
      handled = HandleSetPassphrase(root, result);
    }
    if (command == "unlock-wallet") {
      handled = HandleUnlockWallet(root, result);
    }
    if (command == "encrypt-item") {
      handled = HandleEncryptItem(root, result);
    }
    if (command == "decrypt-item") {
      handled = HandleDecryptItem(root, result);
    }
    if (!handled) {
      result["error_code"] = -999;
    }
    result["id"] = root["id"];
    result["command"] = command;
    Json::StyledWriter writer;
    pp::Var reply_message(writer.write(result));
    PostMessage(reply_message);
  }
};

/// The Module class.  The browser calls the CreateInstance() method
/// to create an instance of your NaCl module on the web page.  The
/// browser creates a new instance for each <embed> tag with
/// type="application/x-pnacl".
class HDWalletDispatcherModule : public pp::Module {
public:
  HDWalletDispatcherModule() : pp::Module() {}
  virtual ~HDWalletDispatcherModule() {}

  /// Create and return a HDWalletDispatcherInstance object.
  /// @param[in] instance The browser-side instance.
  /// @return the plugin-side instance.
  virtual pp::Instance* CreateInstance(PP_Instance instance) {
    return new HDWalletDispatcherInstance(instance);
  }
};

namespace pp {
  /// Factory function called by the browser when the module is first
  /// loaded.  The browser keeps a singleton of this module.  It calls
  /// the CreateInstance() method on the object you return to make
  /// instances.  There is one instance per <embed> tag on the page.
  /// This is the main binding point for your NaCl module with the
  /// browser.
  Module* CreateModule() {
    return new HDWalletDispatcherModule();
  }
}  // namespace pp
