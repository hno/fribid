/*

  Copyright (c) 2009-2010 Samuel Lidén Borell <samuel@slbdata.se>
 
  Permission is hereby granted, free of charge, to any person obtaining a copy
  of this software and associated documentation files (the "Software"), to deal
  in the Software without restriction, including without limitation the rights
  to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
  copies of the Software, and to permit persons to whom the Software is
  furnished to do so, subject to the following conditions:
  
  The above copyright notice and this permission notice shall be included in
  all copies or substantial portions of the Software.
  
  THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
  IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
  FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
  AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
  LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
  OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN
  THE SOFTWARE.

*/

#define _BSD_SOURCE 1

#include <string.h>
#include <stdlib.h>
#include <stdio.h>
#include <assert.h>

#include <libp11.h>

#include <pk11pub.h>
#include <p12.h>
#include <nss.h>
#include <prinit.h>
#include <p12plcy.h>
#include <ciferfam.h>
#include <cert.h>
#include <secitem.h>
#include <secoid.h>
#include <secport.h>
#include <prerror.h>
#include <secerr.h>
#include <cryptohi.h>

#include "../common/defines.h"
#include "misc.h"
#include "platform.h"
#include "keyfile.h"

typedef struct {
    enum {
        PW_NONE = 0,
        PW_FROMFILE = 1,
        PW_PLAINTEXT = 2,
        PW_EXTERNAL = 3
    } source;
    char *data;
} secuPWData;

// Used for storing a dummy NSS database
static char *nssDummyDir;

static void cleanNSSDummyDir() {
    // Remove file names of any files that NSS has created
    // (if the OS supports deletion of filenames of files that are in use)
    PlatformDirIter *iter = platform_openDir(nssDummyDir);
    while (platform_iterateDir(iter)) {
        char *file = platform_currentPath(iter);
        platform_deleteFile(file);
        free(file);
    }
    platform_closeDir(iter);
}

void keyfile_init() {
    PR_Init(PR_SYSTEM_THREAD, PR_PRIORITY_NORMAL, 1);
    
    platform_seedRandom();
    nssDummyDir = platform_makeMemTempDir();
    if (!nssDummyDir) {
        fprintf(stderr, BINNAME ": Failed to create temporary directory!\n");
        abort();
    }
    
    // Initialize NSS
    if (NSS_Initialize(nssDummyDir, "", "", "secmod.db",
            NSS_INIT_NOMODDB | NSS_INIT_NOROOTINIT) != SECSuccess) {
        fprintf(stderr, BINNAME ": NSS initialization failed!\n");
    }
    
    // TODO is this needed?
    SEC_PKCS12EnableCipher(PKCS12_RC2_CBC_40, 1);
    SEC_PKCS12EnableCipher(PKCS12_RC2_CBC_128, 1);
    SEC_PKCS12EnableCipher(PKCS12_RC4_40, 1);
    SEC_PKCS12EnableCipher(PKCS12_RC4_128, 1);
    SEC_PKCS12EnableCipher(PKCS12_DES_56, 1);
    SEC_PKCS12EnableCipher(PKCS12_DES_EDE3_168, 1);
    SEC_PKCS12SetPreferredCipher(PKCS12_DES_EDE3_168, 1);
    
    cleanNSSDummyDir(); // The files are deleted when they are closed
                        // (or on exit if not supported by the OS)
}

void keyfile_shutdown() {
    NSS_Shutdown();
    PR_Cleanup();
    
    cleanNSSDummyDir();
    platform_deleteDir(nssDummyDir);
    free(nssDummyDir);
}

static SEC_PKCS12DecoderContext *pkcs12_open(const char *p12Data, const int p12Length,
                                             const char *password, SECItem **pwitem) {
    
    secuPWData dummy = { PW_NONE, NULL };
    
    // "Key" is important here, otherwise things will silently fail later on
    PK11SlotInfo *slot = PK11_GetInternalKeySlot();
    if (!slot) {
        fprintf(stderr, BINNAME ": got NULL slot\n");
    }
    
    if (PK11_NeedUserInit(slot)) {
        // Create a new encrypted database
        char randomString[13];
        platform_makeRandomString(randomString, 12);
        randomString[12] = '\0';
        
        if (PK11_InitPin(slot, NULL, randomString) != SECSuccess) {
            fprintf(stderr, BINNAME ": failed to set PIN for NSS DB\n");
            return NULL;
        }
    }
    
    if (PK11_Authenticate(slot, PR_TRUE, &dummy) != SECSuccess) {
        fprintf(stderr, BINNAME ": failed to auth slot.\n");
    }
    
    // Convert the password to UCS2
    *pwitem = SECITEM_AllocItem(NULL, NULL, 2*(strlen(password)+1));
    if (!PORT_UCS2_UTF8Conversion(PR_TRUE, (unsigned char*)password, strlen(password)+1,
                                  (*pwitem)->data, (*pwitem)->len,
                                  &(*pwitem)->len)) {
        fprintf(stderr, BINNAME ": failed to convert password\n");
        return NULL;
    }
    
    SEC_PKCS12DecoderContext *decoder = SEC_PKCS12DecoderStart(
            *pwitem, slot, &dummy, NULL, NULL, NULL, NULL, NULL);
    
    if (!decoder)
        return NULL;
    
    // Put the data into the decoder
    if (SEC_PKCS12DecoderUpdate(decoder, (unsigned char*)p12Data, p12Length) != SECSuccess)
        return NULL;
    
    if ((SEC_PKCS12DecoderVerify(decoder) != SECSuccess) &&
        (password[0] != '\0')) {
        fprintf(stderr, BINNAME ": decoder verify failed with "
                        "non-empty password. error = %d\n", PR_GetError());
    }
    
    return decoder;
}

static void pkcs12_close(SEC_PKCS12DecoderContext *decoder, SECItem *pwitem) {
    SEC_PKCS12DecoderFinish(decoder);
    // Clear and free the password
    SECITEM_ZfreeItem(pwitem, PR_TRUE);
}

static CERTCertList *pkcs12_listCerts(const char *p12Data, const int p12Length) {
    
    SECItem *pwitem;
    SEC_PKCS12DecoderContext *decoder = pkcs12_open(p12Data, p12Length, "", &pwitem);
    
    if (!decoder) return NULL;
    
    CERTCertList *certList = SEC_PKCS12DecoderGetCerts(decoder);
    pkcs12_close(decoder, pwitem);
    return certList;
}

static char *der_encode(const CERTCertificate *cert) {
    char *base64 = NULL;
    SECItem *item = SEC_ASN1EncodeItem(NULL, NULL, cert, SEC_ASN1_GET(SEC_SignedCertificateTemplate));
    if (item->type == siBuffer) {
        base64 = base64_encode((char*)item->data, item->len);
    }
    SECITEM_FreeItem(item, PR_TRUE);
    return base64;
}

#define CL_each(node, list) \
        (CERTCertListNode *node = CERT_LIST_HEAD(list); \
         !CERT_LIST_END(node, list); node = CERT_LIST_NEXT(node))

/**
 * Lists the subjects in the given P12 file.
 */
bool keyfile_listPeople(const char *p12Data, const int p12Length,
                        KeyfileSubject ***people, int *count) {
    *count = 0;
    
    CERTCertList *certList = pkcs12_listCerts(p12Data, p12Length);
    if (!certList) return false;
    
    for CL_each(node, certList) {
        if (node->cert->keyUsage & CERTUSE_AUTHENTICATION) (*count)++;
    }
    
    *people = malloc(*count * sizeof(void*));
    KeyfileSubject **person = *people;
    for CL_each(node, certList) {
        if (node->cert->keyUsage & CERTUSE_AUTHENTICATION) {
            *person = strdup(node->cert->subjectName);
            person++;
        }
    }
    
    CERT_DestroyCertList(certList);
    return true;
}

void keyfile_freeSubject(KeyfileSubject *person) {
    free(person);
}

KeyfileSubject *keyfile_duplicateSubject(const KeyfileSubject *person) {
    return strdup(person);
}

bool keyfile_compareSubjects(const KeyfileSubject *a, const KeyfileSubject *b) {
    return (strcmp(a, b) == 0);
}

char *keyfile_getDisplayName(const KeyfileSubject *person) {
    // FIXME: Hack
    const char *name = strstr(person, "OID.2.5.4.41=");
    if (!name) return strdup(person);
    
    name += 13;
    //return strndup(name, strcspn(name, ","));
    int length = strcspn(name, ",");
    char *displayName = malloc(length+1);
    memcpy(displayName, name, length);
    displayName[length] = '\0';
    return displayName;
}

bool keyfile_matchSubjectFilter(const KeyfileSubject *person,
                                const char *subjectFilter) {
    // FIXME: Hack
    if (!subjectFilter) return true;
    
    if ((strncmp(subjectFilter, "2.5.4.5=", 8) != 0) ||
        (strchr(subjectFilter, ',') != NULL)) {
        // OID 2.5.4.5 (Serial number) is the only supported/allowed filter
        return true; // Nothing to filter with
    }
    
    const char *wantedSerial = subjectFilter + 8;
    
    const char *serialOIDTag = strstr(person, "serialNumber=");
    if (!serialOIDTag) {
        // Shouldn't happen
        return true;
    }
    
    const char *actualSerial = serialOIDTag + 13;
    size_t actualLength = strcspn(actualSerial, ",");
    
    return ((strlen(wantedSerial) == actualLength) &&
            (strncmp(wantedSerial, actualSerial, actualLength) == 0));
}

static CERTCertificate *findCert(const CERTCertList *certList,
                                 const KeyfileSubject *person,
                                 const unsigned int certMask) {
    for CL_each(node, certList) {
        if (((node->cert->keyUsage & certMask) == certMask) &&
             !strcmp(node->cert->subjectName, person)) {
            return node->cert;
        }
    }
    return NULL;
}


/**
 * TODO
 */
bool smartcard_getBase64Chain(PKCS11_SLOT *slot,
                              char ***certs, int *count) {
    int rc = 0;
    unsigned int ncerts;
    PKCS11_CERT *sccerts;

    rc = PKCS11_enumerate_certs(slot->token, &sccerts, &ncerts);
    
    if (!ncerts) return false;

    *count = ncerts;
    *certs = malloc(sizeof(char*));
    *certs = realloc(*certs, *count * sizeof(char*));
    for (unsigned int i = 0; i <= ncerts-1; i++) {
        unsigned char *buf;
        int len;
        buf = NULL;
        len = i2d_X509(sccerts[i].x509, &buf);
        char *base64 = NULL;
        base64 = base64_encode((const char*)buf, len);
        (*certs)[i] = base64;
        free(buf);
    }
    
    return true;
}


/**
 * Returns a list of DER-BASE64 encoded certificates, from the subject
 * to the root CA. This is actually wrong, since the root CA that's
 * returned could be untrusted. However, at least my P12 has only one
 * possible chain and the validation is done server-side, so this shouldn't
 * be a problem.
 */
bool keyfile_getBase64Chain(const char *p12Data, const int p12Length,
                            const KeyfileSubject *person,
                            const unsigned int certMask,
                            char ***certs, int *count) {
    
    CERTCertList *certList = pkcs12_listCerts(p12Data, p12Length);
    if (!certList) return false;
    
    CERTCertificate *cert = findCert(certList, person, certMask);
    if (!cert) {
        CERT_DestroyCertList(certList);
        return false;
    }
    
    *count = 1;
    *certs = malloc(sizeof(char*));
    (*certs)[0] = der_encode(cert);
    
    while (cert->issuerName != NULL) {
        cert = findCert(certList, cert->issuerName, CERTUSE_ISSUER);
        if (!cert) break;
        
        (*count)++;
        *certs = realloc(*certs, *count * sizeof(char*));
        (*certs)[*count-1] = der_encode(cert);
    }
    CERT_DestroyCertList(certList);
    return true;
}

/**
 * This function is needed by NSS
 */
static SECItem *nicknameCollisionFunction(SECItem *oldNick, PRBool *cancel, void *wincx) {
    CERTCertificate* cert = (CERTCertificate*)wincx;
    
    if (!cert || (cancel == NULL)) return NULL;
    
    char *caNick = CERT_MakeCANickname(cert);
    if (!caNick) return NULL;
    
    if (oldNick && oldNick->data && (oldNick->len != 0) &&
        (oldNick->len == strlen(caNick)) &&
        !strncmp((const char*)oldNick->data, caNick, oldNick->len)) {
        // Equal
        PORT_Free(caNick);
        PORT_SetError(SEC_ERROR_IO);
        return NULL;
    }
    
    SECItem *item = PORT_New(SECItem);
    if (!item) {
        PORT_Free(caNick);
        return NULL;
    }
    item->type = siBuffer;
    item->len = strlen(caNick);
    item->data = (unsigned char*)caNick;
    
    return item;
}


bool keyfile_sign(const char *p12Data, const int p12Length,
                  const KeyfileSubject *person,
                  const unsigned int certMask,
                  const char *password,
                  const char *message, const int messagelen,
                  char **signature, int *siglen) {
    
    assert(p12Data != NULL);
    assert(person != NULL);
    assert(message != NULL);
    assert(password != NULL);
    assert(signature != NULL);
    assert(siglen != NULL);
    
    SECItem *pwitem;
    SEC_PKCS12DecoderContext *decoder = pkcs12_open(p12Data, p12Length, password, &pwitem);
    if (!decoder) return false;
    
    if (SEC_PKCS12DecoderValidateBags(decoder, nicknameCollisionFunction) != SECSuccess) {
        fprintf(stderr, BINNAME ": failed to validate \"bags\". error = %d\n", PR_GetError());
        pkcs12_close(decoder, pwitem);
        return false;
    }
    
    if (SEC_PKCS12DecoderImportBags(decoder) != SECSuccess) {
        fprintf(stderr, BINNAME ": failed to import \"bags\". error = %d\n", PR_GetError());
        pkcs12_close(decoder, pwitem);
        return false;
    }
    CERTCertList *certList = SEC_PKCS12DecoderGetCerts(decoder);
    pkcs12_close(decoder, pwitem);
    
    for CL_each(node, certList) {
        if (((node->cert->keyUsage & certMask) == certMask) &&
             !strcmp(node->cert->subjectName, person)) {
             
            secuPWData dummy = { PW_NONE, NULL };
            SECKEYPrivateKey *privkey = PK11_FindPrivateKeyFromCert(PK11_GetInternalKeySlot(), node->cert, &dummy);
            if (!privkey) {
                CERT_DestroyCertList(certList);
                return false;
            }
            
            SECItem result = { siBuffer, NULL, 0 };
            if (SEC_SignData(&result, (unsigned char *)message, messagelen, privkey,
                             SEC_OID_ISO_SHA_WITH_RSA_SIGNATURE) != SECSuccess) {
                fprintf(stderr, BINNAME ": failed to sign data!\n");
                SECKEY_DestroyPrivateKey(privkey);
                CERT_DestroyCertList(certList);
                return false;
            }
            
            SECKEY_DestroyPrivateKey(privkey);
            
            *signature = malloc(result.len);
            memcpy(*signature, result.data, result.len);
            *siglen = result.len;
            SECITEM_FreeItem(&result, PR_FALSE);
            
            CERT_DestroyCertList(certList);
            return true;
        }
    }
    
    CERT_DestroyCertList(certList);
    return false;
}

