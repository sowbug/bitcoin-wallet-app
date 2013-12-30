#include "node.h"

#include <algorithm>
#include <iomanip>
#include <iostream>
#include <iterator>
#include <sstream>
#include <stdint.h>

#include "base58.h"
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

  virtual bool HandleGetWalletNode(const Json::Value& args,
                                   Json::Value& result) {
    const std::string seed_hex = args.get("seed_hex", "").asString();
    bytes_t seed_bytes(seed_hex.size() / 2);
    unhexlify(&seed_hex[0],
              &seed_hex[0] + seed_hex.size(),
              &seed_bytes[0]);

    const std::string node_path = args.get("path", "m").asString();
    std::istringstream iss(node_path);
    std::string token;
    std::vector<std::string> node_path_parts;
    while (std::getline(iss, token, '/')) {
      node_path_parts.push_back(token);
    }

    Node *node = NULL;
    if (seed_bytes.size() == 78) {
      node = NodeFactory::CreateNodeFromExtended(seed_bytes);
    } else if (seed_hex[0] == 'x') {
      node = NodeFactory::CreateNodeFromExtended(Base58::fromBase58Check(seed_hex));
    } else {
      node = NodeFactory::CreateNodeFromSeed(seed_bytes);
    }
    for (size_t i = 1; i < node_path_parts.size(); ++i) {
      std::string part = node_path_parts[i];
      uint32_t n = strtol(&part[0], NULL, 10);
      if (part.rfind('\'') != std::string::npos) {
        n += 0x80000000;
      }
      Node *child_node = NodeFactory::DeriveChildNode(*node, n);
      delete node;
      node = child_node;
    }

    result["secret_key"] = to_hex(node->secret_key());
    result["chain_code"] = to_hex(node->chain_code());
    result["public_key"] = to_hex(node->public_key());
    {
      std::stringstream stream;
      stream << "0x"
             << std::setfill ('0') << std::setw(sizeof(uint32_t) * 2)
             << std::hex << node->fingerprint();
      result["fingerprint"] = stream.str();
    }
    result["node"] = node->toString();

    delete node;

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
    if (command == "get-wallet-node") {
      handled = HandleGetWalletNode(root, result);
    }
    if (handled) {
      result["command"] = command;
      Json::StyledWriter writer;
      pp::Var reply_message(writer.write(result));
      PostMessage(reply_message);
    }
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
