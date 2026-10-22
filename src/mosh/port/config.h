/* The build configuration of mosh's client sources (third_party/mosh) on
   Windows, in place of the config.h that mosh's configure script writes. */

#ifndef MOSH_PORT_CONFIG_H
#define MOSH_PORT_CONFIG_H

#define PACKAGE_STRING "mosh 1.4.0"

/* std::shared_ptr and std::make_shared (shared.h). */
#define HAVE_MEMORY 1
#define HAVE_STD_SHARED_PTR 1

/* AES for the OCB code in ocb_internal.cc comes from OpenSSL's EVP API. */
#define USE_OPENSSL_AES 1

#if defined(__GNUC__)
#define HAVE_DECL___BUILTIN_BSWAP64 1
#define HAVE_DECL___BUILTIN_CTZ 1
#endif

#endif
