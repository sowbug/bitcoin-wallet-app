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

#include <string>
#include <vector>

#include "types.h"

class UnspentTxo {
 public:
  bytes_t GetSigningAddress();

  bytes_t hash;
  uint32_t output_n;
  bytes_t script;
  uint64_t value;
};
typedef std::vector<UnspentTxo> unspent_txos_t;

struct TxOut {
  bytes_t hash;
  uint64_t value;

  TxOut(const bytes_t& hash, uint64_t value);
};
typedef std::vector<TxOut> tx_outs_t;

class Node;

class Tx {
 public:
  static bool CreateSignedTransaction(const Node& sending_node,
                                      const unspent_txos_t& unspent_txos,
                                      const bytes_t& recipient_hash160,
                                      uint64_t value,
                                      uint64_t fee,
                                      uint32_t change_index,
                                      bytes_t& signed_tx);
};
