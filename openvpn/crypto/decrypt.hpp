//
//  decrypt.hpp
//  OpenVPN
//
//  Copyright (c) 2012 OpenVPN Technologies, Inc. All rights reserved.
//

#ifndef OPENVPN_CRYPTO_DECRYPT_H
#define OPENVPN_CRYPTO_DECRYPT_H

#include <cstring>

#include <openvpn/common/types.hpp>
#include <openvpn/common/exception.hpp>
#include <openvpn/common/memcmp.hpp>
#include <openvpn/buffer/buffer.hpp>
#include <openvpn/random/prng.hpp>
#include <openvpn/frame/frame.hpp>
#include <openvpn/crypto/cipher.hpp>
#include <openvpn/crypto/hmac.hpp>
#include <openvpn/crypto/static_key.hpp>
#include <openvpn/crypto/packet_id.hpp>
#include <openvpn/log/sessionstats.hpp>

namespace openvpn {

  template <typename CRYPTO_API>
  class Decrypt {
  public:
    OPENVPN_SIMPLE_EXCEPTION(unsupported_cipher_mode);

    Error::Type decrypt(BufferAllocated& buf, const PacketID::time_t now)
    {
      // skip null packets
      if (!buf.size())
	return Error::SUCCESS;

      // verify the HMAC
      if (hmac.defined())
	{
	  unsigned char local_hmac[CRYPTO_API::HMACContext::MAX_HMAC_SIZE];
	  const size_t hmac_size = hmac.output_size();
	  const unsigned char *packet_hmac = buf.read_alloc(hmac_size);
	  hmac.hmac(local_hmac, hmac_size, buf.c_data(), buf.size());
	  if (memcmp_secure(local_hmac, packet_hmac, hmac_size))
	    {
	      buf.reset_size();
	      return Error::HMAC_ERROR;
	    }
	}

      // decrypt packet ID + payload
      if (cipher.defined())
	{
	  unsigned char iv_buf[CRYPTO_API::CipherContext::MAX_IV_LENGTH];
	  const size_t iv_length = cipher.iv_length();

	  // extract IV from head of packet
	  buf.read(iv_buf, iv_length);

	  // initialize work buffer
	  frame->prepare(Frame::DECRYPT_WORK, work);

	  // decrypt from buf -> work
	  const size_t decrypt_bytes = cipher.decrypt(iv_buf, work.data(), work.max_size(), buf.c_data(), buf.size());
	  if (!decrypt_bytes)
	    {
	      buf.reset_size();
	      return Error::DECRYPT_ERROR;
	    }
	  work.set_size(decrypt_bytes);

	  // handle different cipher modes
	  const int cipher_mode = cipher.cipher_mode();
	  if (cipher_mode == CRYPTO_API::CipherContext::CIPH_CBC_MODE)
	    {
	      if (!verify_packet_id(work, now))
		{
		  buf.reset_size();
		  return Error::REPLAY_ERROR;
		}
	    }
	  else
	    {
	      throw unsupported_cipher_mode();
	    }

	  // return cleartext result in buf
	  buf.swap(work);
	}
      else // no encryption
	{
	  if (!verify_packet_id(buf, now))
	    {
	      buf.reset_size();
	      return Error::REPLAY_ERROR;
	    }
	}
      return Error::SUCCESS;
    }

    Frame::Ptr frame;
    CipherContext<CRYPTO_API> cipher;
    HMACContext<CRYPTO_API> hmac;
    PacketIDReceive pid_recv;

  private:
    bool verify_packet_id(BufferAllocated& buf, const PacketID::time_t now)
    {
      // ignore packet ID if pid_recv is not initialized
      if (pid_recv.initialized())
	{
	  const PacketID pid = pid_recv.read_next(buf);
	  if (pid_recv.test(pid, now)) // verify packet ID
	    pid_recv.add(pid, now);    // remember packet ID
	  else
	    return false;
	}
      return true;
    }

    BufferAllocated work;
  };

} // namespace openvpn

#endif // OPENVPN_CRYPTO_DECRYPT_H
