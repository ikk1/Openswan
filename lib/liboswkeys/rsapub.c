/*
 * mechanisms for preshared keys (public, private, and preshared secrets)
 * this is the library for reading (and later, writing!) the ipsec.secrets
 * files.
 *
 * Copyright (C) 1998-2004  D. Hugh Redelmeier.
 * Copyright (C) 2017 Michael Richardson <mcr@xelerance.com>
 */

#include <stddef.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include <unistd.h>
#include <errno.h>
#include <time.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <arpa/nameser.h>	/* missing from <resolv.h> on old systems */

#include <gmp.h>
#include <openswan.h>
#include <openswan/ipsec_policy.h>

#include "sysdep.h"
#include "oswlog.h"
#include "constants.h"
#include "oswalloc.h"
#include "oswtime.h"
#include "id.h"
#include "secrets_int.h"
#include "secrets.h"
#include "mpzfuncs.h"


static void
RSA_show_key_field(const char *name, MP_INT *num)
{
    int sz;
    char buf[RSA_MAX_OCTETS * 2 + 2];	/* ought to be big enough */

    sz = mpz_sizeinbase(num, 16);
    passert(sz <= sizeof(buf));
    mpz_get_str(buf, 16, num);

    DBG_log(" %s: %s", name, buf);
}

void
RSA_show_key_fields(struct private_key_stuff *pks)
{
    DBG_log(" keyid: *%s", pks->pub->u.rsa.keyid);

    RSA_show_key_field("Modulus", &pks->pub->u.rsa.n);
    RSA_show_key_field("PublicExponent", &pks->pub->u.rsa.e);

    RSA_show_key_field("PrivateExponent", &pks->u.RSA_private_key.d);
    RSA_show_key_field("Prime1", &pks->u.RSA_private_key.p);
    RSA_show_key_field("Prime2", &pks->u.RSA_private_key.q);
    RSA_show_key_field("Exponent1", &pks->u.RSA_private_key.dP);
    RSA_show_key_field("Exponent2", &pks->u.RSA_private_key.dQ);
    RSA_show_key_field("Coefficient", &pks->u.RSA_private_key.qInv);
}


/* decode of RSA pubkey chunk
 * - format specified in RFC 2537 RSA/MD5 Keys and SIGs in the DNS
 * - exponent length in bytes (1 or 3 octets)
 *   + 1 byte if in [1, 255]
 *   + otherwise 0x00 followed by 2 bytes of length
 * - exponent
 * - modulus
 */
err_t
unpack_RSA_public_key(struct RSA_public_key *rsa, const chunk_t *pubkey)
{
    chunk_t exponent;
    chunk_t mod;

    rsa->keyid[0] = '\0';	/* in case of keybolbtoid failure */

    if (pubkey->len < 3)
	return "RSA public key blob way to short";	/* not even room for length! */

    rsa->key_rfc3110 = chunk_clone(*pubkey, "rfc3110 format of public key");

    if (pubkey->ptr[0] != 0x00)
    {
	setchunk(exponent, pubkey->ptr + 1, pubkey->ptr[0]);
    }
    else
    {
	setchunk(exponent, pubkey->ptr + 3
	    , (pubkey->ptr[1] << BITS_PER_BYTE) + pubkey->ptr[2]);
    }

    if (pubkey->len - (exponent.ptr - pubkey->ptr) < exponent.len + RSA_MIN_OCTETS_RFC)
	return "RSA public key blob too short";

    mod.ptr = exponent.ptr + exponent.len;
    mod.len = &pubkey->ptr[pubkey->len] - mod.ptr;

    if (mod.len < RSA_MIN_OCTETS)
	return RSA_MIN_OCTETS_UGH;

    if (mod.len > RSA_MAX_OCTETS)
	return RSA_MAX_OCTETS_UGH;

    if (mod.len > pubkey->ptr + pubkey->len - mod.ptr)
       return "RSA public key blob too short";

    n_to_mpz(&rsa->e, exponent.ptr, exponent.len);
    n_to_mpz(&rsa->n, mod.ptr, mod.len);

    keyblobtoid(pubkey->ptr, pubkey->len, rsa->keyid, sizeof(rsa->keyid));

    rsa->k = mpz_sizeinbase(&rsa->n, 2);	/* size in bits, for a start */
    rsa->k = (rsa->k + BITS_PER_BYTE - 1) / BITS_PER_BYTE;	/* now octets */

    if (rsa->k != mod.len)
    {
	mpz_clear(&rsa->e);
	mpz_clear(&rsa->n);
	return "RSA modulus shorter than specified";
    }

    return NULL;
}

/*
 * Local Variables:
 * c-basic-offset:4
 * c-style: pluto
 * End:
 */