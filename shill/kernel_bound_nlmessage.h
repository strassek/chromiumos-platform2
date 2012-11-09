// Copyright (c) 2012 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.
//
// This code is derived from the 'iw' source code.  The copyright and license
// of that code is as follows:
//
// Copyright (c) 2007, 2008  Johannes Berg
// Copyright (c) 2007  Andy Lutomirski
// Copyright (c) 2007  Mike Kershaw
// Copyright (c) 2008-2009  Luis R. Rodriguez
//
// Permission to use, copy, modify, and/or distribute this software for any
// purpose with or without fee is hereby granted, provided that the above
// copyright notice and this permission notice appear in all copies.
//
// THE SOFTWARE IS PROVIDED "AS IS" AND THE AUTHOR DISCLAIMS ALL WARRANTIES
// WITH REGARD TO THIS SOFTWARE INCLUDING ALL IMPLIED WARRANTIES OF
// MERCHANTABILITY AND FITNESS. IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR
// ANY SPECIAL, DIRECT, INDIRECT, OR CONSEQUENTIAL DAMAGES OR ANY DAMAGES
// WHATSOEVER RESULTING FROM LOSS OF USE, DATA OR PROFITS, WHETHER IN AN
// ACTION OF CONTRACT, NEGLIGENCE OR OTHER TORTIOUS ACTION, ARISING OUT OF
// OR IN CONNECTION WITH THE USE OR PERFORMANCE OF THIS SOFTWARE.

#ifndef SHILL_KERNEL_BOUND_NLMESSAGE_H_
#define SHILL_KERNEL_BOUND_NLMESSAGE_H_

#include <base/basictypes.h>

struct nl_msg;

namespace shill {

// TODO(wdg): eventually, KernelBoundNlMessage and UserBoundNlMessage should
// be combined into a monolithic NlMessage.
//
// Provides a wrapper around a netlink message destined for kernel-space.
class KernelBoundNlMessage {
 public:
  // |command| is a type of command understood by the kernel, for instance:
  // CTRL_CMD_GETFAMILY.
  explicit KernelBoundNlMessage(uint8 command)
      : command_(command),
        message_(NULL) {};
  virtual ~KernelBoundNlMessage();

  // Non-trivial initialization.
  bool Init();

  // Add a netlink attribute to the message.
  int AddAttribute(int attrtype, int attrlen, const void *data);

  uint8 command() const { return command_; }
  // TODO(wiley) It would be better if messages were bags of attributes which
  //             the socket collapses into binary blobs at send time.
  struct nl_msg *message() const { return message_; }
  // Returns 0 when unsent, > 0 otherwise.
  uint32 sequence_number() const;

 private:
  uint8 command_;
  // TODO(wiley) Rename to |raw_message_| (message.message() looks silly).
  struct nl_msg *message_;

  DISALLOW_COPY_AND_ASSIGN(KernelBoundNlMessage);
};

}  // namespace shill

#endif  // SHILL_KERNEL_BOUND_NLMESSAGE_H_
