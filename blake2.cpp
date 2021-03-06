// blake2.cpp - written and placed in the public domain by Jeffrey Walton
//              and Zooko Wilcox-O'Hearn. Based on Aumasson, Neves,
//              Wilcox-O'Hearn and Winnerlein's reference BLAKE2
//              implementation at http://github.com/BLAKE2/BLAKE2.
//
// The BLAKE2b and BLAKE2s numbers are consistent with the BLAKE2 team's
// numbers. However, we have an Altivec/POWER7 implementation of BLAKE2s,
// and a POWER8 implementation of BLAKE2b (BLAKE2 is missing them). The
// Altivec/POWER7 code is about 2x faster than C++ when using GCC 5.0 or
// above. The POWER8 code is about 2.5x faster than C++ when using GCC 5.0
// or above. If you use GCC 4.0 (PowerMac) or GCC 4.8 (GCC Compile Farm)
// then the PowerPC code will be slower than C++. Be sure to use GCC 5.0
// or above for PowerPC builds or disable Altivec for BLAKE2b and BLAKE2s
// if using the old compilers.

#include "pch.h"
#include "config.h"
#include "cryptlib.h"
#include "argnames.h"
#include "algparam.h"
#include "blake2.h"
#include "cpu.h"

// Uncomment for benchmarking C++ against SSE2 or NEON.
// Do so in both blake2.cpp and blake2-simd.cpp.
// #undef CRYPTOPP_SSE41_AVAILABLE
// #undef CRYPTOPP_ARM_NEON_AVAILABLE
// #undef CRYPTOPP_ALTIVEC_AVAILABLE
// #undef CRYPTOPP_POWER8_AVAILABLE

// Disable NEON/ASIMD for Cortex-A53 and A57. The shifts are too slow and C/C++ is about
// 3 cpb faster than NEON/ASIMD. Also see http://github.com/weidai11/cryptopp/issues/367.
#if (defined(__aarch32__) || defined(__aarch64__)) && defined(CRYPTOPP_SLOW_ARMV8_SHIFT)
# undef CRYPTOPP_ARM_NEON_AVAILABLE
#endif

NAMESPACE_BEGIN(CryptoPP)

// Export the tables to the SIMD files
extern const word32 BLAKE2S_IV[8];
extern const word64 BLAKE2B_IV[8];

CRYPTOPP_ALIGN_DATA(16)
const word32 BLAKE2S_IV[8] = {
    0x6A09E667UL, 0xBB67AE85UL, 0x3C6EF372UL, 0xA54FF53AUL,
    0x510E527FUL, 0x9B05688CUL, 0x1F83D9ABUL, 0x5BE0CD19UL
};

CRYPTOPP_ALIGN_DATA(16)
const word64 BLAKE2B_IV[8] = {
    W64LIT(0x6a09e667f3bcc908), W64LIT(0xbb67ae8584caa73b),
    W64LIT(0x3c6ef372fe94f82b), W64LIT(0xa54ff53a5f1d36f1),
    W64LIT(0x510e527fade682d1), W64LIT(0x9b05688c2b3e6c1f),
    W64LIT(0x1f83d9abfb41bd6b), W64LIT(0x5be0cd19137e2179)
};

NAMESPACE_END

ANONYMOUS_NAMESPACE_BEGIN

using CryptoPP::byte;
using CryptoPP::word32;
using CryptoPP::word64;
using CryptoPP::rotrConstant;

CRYPTOPP_ALIGN_DATA(16)
const byte BLAKE2S_SIGMA[10][16] = {
    {  0,  1,  2,  3,  4,  5,  6,  7,  8,  9, 10, 11, 12, 13, 14, 15 },
    { 14, 10,  4,  8,  9, 15, 13,  6,  1, 12,  0,  2, 11,  7,  5,  3 },
    { 11,  8, 12,  0,  5,  2, 15, 13, 10, 14,  3,  6,  7,  1,  9,  4 },
    {  7,  9,  3,  1, 13, 12, 11, 14,  2,  6,  5, 10,  4,  0, 15,  8 },
    {  9,  0,  5,  7,  2,  4, 10, 15, 14,  1, 11, 12,  6,  8,  3, 13 },
    {  2, 12,  6, 10,  0, 11,  8,  3,  4, 13,  7,  5, 15, 14,  1,  9 },
    { 12,  5,  1, 15, 14, 13,  4, 10,  0,  7,  6,  3,  9,  2,  8, 11 },
    { 13, 11,  7, 14, 12,  1,  3,  9,  5,  0, 15,  4,  8,  6,  2, 10 },
    {  6, 15, 14,  9, 11,  3,  0,  8, 12,  2, 13,  7,  1,  4, 10,  5 },
    { 10,  2,  8,  4,  7,  6,  1,  5, 15, 11,  9, 14,  3, 12, 13 , 0 },
};

CRYPTOPP_ALIGN_DATA(16)
const byte BLAKE2B_SIGMA[12][16] = {
    {  0,  1,  2,  3,  4,  5,  6,  7,  8,  9, 10, 11, 12, 13, 14, 15 },
    { 14, 10,  4,  8,  9, 15, 13,  6,  1, 12,  0,  2, 11,  7,  5,  3 },
    { 11,  8, 12,  0,  5,  2, 15, 13, 10, 14,  3,  6,  7,  1,  9,  4 },
    {  7,  9,  3,  1, 13, 12, 11, 14,  2,  6,  5, 10,  4,  0, 15,  8 },
    {  9,  0,  5,  7,  2,  4, 10, 15, 14,  1, 11, 12,  6,  8,  3, 13 },
    {  2, 12,  6, 10,  0, 11,  8,  3,  4, 13,  7,  5, 15, 14,  1,  9 },
    { 12,  5,  1, 15, 14, 13,  4, 10,  0,  7,  6,  3,  9,  2,  8, 11 },
    { 13, 11,  7, 14, 12,  1,  3,  9,  5,  0, 15,  4,  8,  6,  2, 10 },
    {  6, 15, 14,  9, 11,  3,  0,  8, 12,  2, 13,  7,  1,  4, 10,  5 },
    { 10,  2,  8,  4,  7,  6,  1,  5, 15, 11,  9, 14,  3, 12, 13 , 0 },
    {  0,  1,  2,  3,  4,  5,  6,  7,  8,  9, 10, 11, 12, 13, 14, 15 },
    { 14, 10,  4,  8,  9, 15, 13,  6,  1, 12,  0,  2, 11,  7,  5,  3 }
};

template <unsigned int R, unsigned int N>
inline void BLAKE2B_G(const word64 m[16], word64& a, word64& b, word64& c, word64& d)
{
    a = a + b + m[BLAKE2B_SIGMA[R][2*N+0]];
    d = rotrConstant<32>(d ^ a);
    c = c + d;
    b = rotrConstant<24>(b ^ c);
    a = a + b + m[BLAKE2B_SIGMA[R][2*N+1]];
    d = rotrConstant<16>(d ^ a);
    c = c + d;
    b = rotrConstant<63>(b ^ c);
}

template <unsigned int R>
inline void BLAKE2B_ROUND(const word64 m[16], word64 v[16])
{
    BLAKE2B_G<R,0>(m,v[ 0],v[ 4],v[ 8],v[12]);
    BLAKE2B_G<R,1>(m,v[ 1],v[ 5],v[ 9],v[13]);
    BLAKE2B_G<R,2>(m,v[ 2],v[ 6],v[10],v[14]);
    BLAKE2B_G<R,3>(m,v[ 3],v[ 7],v[11],v[15]);
    BLAKE2B_G<R,4>(m,v[ 0],v[ 5],v[10],v[15]);
    BLAKE2B_G<R,5>(m,v[ 1],v[ 6],v[11],v[12]);
    BLAKE2B_G<R,6>(m,v[ 2],v[ 7],v[ 8],v[13]);
    BLAKE2B_G<R,7>(m,v[ 3],v[ 4],v[ 9],v[14]);
}

template <unsigned int R, unsigned int N>
inline void BLAKE2S_G(const word32 m[16], word32& a, word32& b, word32& c, word32& d)
{
    a = a + b + m[BLAKE2S_SIGMA[R][2*N+0]];
    d = rotrConstant<16>(d ^ a);
    c = c + d;
    b = rotrConstant<12>(b ^ c);
    a = a + b + m[BLAKE2S_SIGMA[R][2*N+1]];
    d = rotrConstant<8>(d ^ a);
    c = c + d;
    b = rotrConstant<7>(b ^ c);
}

template <unsigned int R>
inline void BLAKE2S_ROUND(const word32 m[16], word32 v[])
{
    BLAKE2S_G<R,0>(m,v[ 0],v[ 4],v[ 8],v[12]);
    BLAKE2S_G<R,1>(m,v[ 1],v[ 5],v[ 9],v[13]);
    BLAKE2S_G<R,2>(m,v[ 2],v[ 6],v[10],v[14]);
    BLAKE2S_G<R,3>(m,v[ 3],v[ 7],v[11],v[15]);
    BLAKE2S_G<R,4>(m,v[ 0],v[ 5],v[10],v[15]);
    BLAKE2S_G<R,5>(m,v[ 1],v[ 6],v[11],v[12]);
    BLAKE2S_G<R,6>(m,v[ 2],v[ 7],v[ 8],v[13]);
    BLAKE2S_G<R,7>(m,v[ 3],v[ 4],v[ 9],v[14]);
}

ANONYMOUS_NAMESPACE_END

NAMESPACE_BEGIN(CryptoPP)

void BLAKE2_Compress32_CXX(const byte* input, BLAKE2s_State& state);
void BLAKE2_Compress64_CXX(const byte* input, BLAKE2b_State& state);

#if CRYPTOPP_SSE41_AVAILABLE
extern void BLAKE2_Compress32_SSE4(const byte* input, BLAKE2s_State& state);
extern void BLAKE2_Compress64_SSE4(const byte* input, BLAKE2b_State& state);
#endif

#if CRYPTOPP_ARM_NEON_AVAILABLE
extern void BLAKE2_Compress32_NEON(const byte* input, BLAKE2s_State& state);
extern void BLAKE2_Compress64_NEON(const byte* input, BLAKE2b_State& state);
#endif

#if CRYPTOPP_ALTIVEC_AVAILABLE
extern void BLAKE2_Compress32_POWER7(const byte* input, BLAKE2s_State& state);
#endif

#if CRYPTOPP_POWER8_AVAILABLE
extern void BLAKE2_Compress64_POWER8(const byte* input, BLAKE2b_State& state);
#endif

BLAKE2s_ParameterBlock::BLAKE2s_ParameterBlock(size_t digestLen, size_t keyLen,
        const byte* saltStr, size_t saltLen,
        const byte* personalizationStr, size_t personalizationLen)
{
    digestLength = (byte)digestLen;
    keyLength = (byte)keyLen;
    fanout = depth = 1;
    nodeDepth = innerLength = 0;

    std::memset(leafLength, 0x00, COUNTOF(leafLength));
    std::memset(nodeOffset, 0x00, COUNTOF(nodeOffset));

    if (saltStr && saltLen)
    {
        memcpy_s(salt, COUNTOF(salt), saltStr, saltLen);
        size_t rem = SaturatingSubtract(COUNTOF(salt), saltLen);
        size_t off = COUNTOF(salt) - rem;
        if (rem)
            std::memset(salt+off, 0x00, rem);
    }
    else
    {
        std::memset(salt, 0x00, COUNTOF(salt));
    }

    if (personalizationStr && personalizationLen)
    {
        memcpy_s(personalization, COUNTOF(personalization), personalizationStr, personalizationLen);
        size_t rem = SaturatingSubtract(COUNTOF(personalization), personalizationLen);
        size_t off = COUNTOF(personalization) - rem;
        if (rem)
            std::memset(personalization+off, 0x00, rem);
    }
    else
    {
        std::memset(personalization, 0x00, COUNTOF(personalization));
    }
}

BLAKE2b_ParameterBlock::BLAKE2b_ParameterBlock(size_t digestLen, size_t keyLen,
        const byte* saltStr, size_t saltLen,
        const byte* personalizationStr, size_t personalizationLen)
{
    digestLength = (byte)digestLen;
    keyLength = (byte)keyLen;
    fanout = depth = 1;
    nodeDepth = innerLength = 0;

    std::memset(rfu, 0x00, COUNTOF(rfu));
    std::memset(leafLength, 0x00, COUNTOF(leafLength));
    std::memset(nodeOffset, 0x00, COUNTOF(nodeOffset));

    if (saltStr && saltLen)
    {
        memcpy_s(salt, COUNTOF(salt), saltStr, saltLen);
        size_t rem = SaturatingSubtract(COUNTOF(salt), saltLen);
        size_t off = COUNTOF(salt) - rem;
        if (rem)
            std::memset(salt+off, 0x00, rem);
    }
    else
    {
        std::memset(salt, 0x00, COUNTOF(salt));
    }

    if (personalizationStr && personalizationLen)
    {
        memcpy_s(personalization, COUNTOF(personalization), personalizationStr, personalizationLen);
        size_t rem = SaturatingSubtract(COUNTOF(personalization), personalizationLen);
        size_t off = COUNTOF(personalization) - rem;
        if (rem)
            std::memset(personalization+off, 0x00, rem);
    }
    else
    {
        std::memset(personalization, 0x00, COUNTOF(personalization));
    }
}

void BLAKE2s::UncheckedSetKey(const byte *key, unsigned int length, const CryptoPP::NameValuePairs& params)
{
    if (key && length)
    {
        AlignedSecByteBlock temp(BLOCKSIZE);
        memcpy_s(temp, BLOCKSIZE, key, length);

        size_t rem = SaturatingSubtract((unsigned int)BLOCKSIZE, length);
        if (rem)
            std::memset(temp+length, 0x00, rem);

        m_key.swap(temp);
    }
    else
    {
        m_key.resize(0);
    }

    ParameterBlock& block = *m_block.data();
    std::memset(block.leafLength, 0x00, COUNTOF(block.leafLength));
    std::memset(block.nodeOffset, 0x00, COUNTOF(block.nodeOffset));

    block.nodeDepth = block.innerLength = 0;
    block.keyLength = (byte)length;
    block.digestLength = (byte)params.GetIntValueWithDefault(Name::DigestSize(), DIGESTSIZE);
    block.fanout = block.depth = 1;

    ConstByteArrayParameter t;
    if (params.GetValue(Name::Salt(), t) && t.begin() && t.size())
    {
        memcpy_s(block.salt, COUNTOF(block.salt), t.begin(), t.size());
        size_t rem = SaturatingSubtract(COUNTOF(block.salt), t.size());
        size_t off = COUNTOF(block.salt) - rem;
        if (rem)
            std::memset(block.salt+off, 0x00, rem);
    }
    else
    {
        std::memset(block.salt, 0x00, COUNTOF(block.salt));
    }

    if (params.GetValue(Name::Personalization(), t) && t.begin() && t.size())
    {
        memcpy_s(block.personalization, COUNTOF(block.personalization), t.begin(), t.size());
        size_t rem = SaturatingSubtract(COUNTOF(block.personalization), t.size());
        size_t off = COUNTOF(block.personalization) - rem;
        if (rem)
            std::memset(block.personalization+off, 0x00, rem);
    }
    else
    {
        std::memset(block.personalization, 0x00, COUNTOF(block.personalization));
    }
}

void BLAKE2b::UncheckedSetKey(const byte *key, unsigned int length, const CryptoPP::NameValuePairs& params)
{
    if (key && length)
    {
        AlignedSecByteBlock temp(BLOCKSIZE);
        memcpy_s(temp, BLOCKSIZE, key, length);

        size_t rem = SaturatingSubtract((unsigned int)BLOCKSIZE, length);
        if (rem)
            std::memset(temp+length, 0x00, rem);

        m_key.swap(temp);
    }
    else
    {
        m_key.resize(0);
    }

    ParameterBlock& block = *m_block.data();
    std::memset(block.leafLength, 0x00, COUNTOF(block.leafLength));
    std::memset(block.nodeOffset, 0x00, COUNTOF(block.nodeOffset));
    std::memset(block.rfu, 0x00, COUNTOF(block.rfu));

    block.nodeDepth = block.innerLength = 0;
    block.keyLength = (byte)length;
    block.digestLength = (byte)params.GetIntValueWithDefault(Name::DigestSize(), DIGESTSIZE);
    block.fanout = block.depth = 1;

    ConstByteArrayParameter t;
    if (params.GetValue(Name::Salt(), t) && t.begin() && t.size())
    {
        memcpy_s(block.salt, COUNTOF(block.salt), t.begin(), t.size());
        size_t rem = SaturatingSubtract(COUNTOF(block.salt), t.size());
        size_t off = COUNTOF(block.salt) - rem;
        if (rem)
            std::memset(block.salt+off, 0x00, rem);
    }
    else
    {
        std::memset(block.salt, 0x00, COUNTOF(block.salt));
    }

    if (params.GetValue(Name::Personalization(), t) && t.begin() && t.size())
    {
        memcpy_s(block.personalization, COUNTOF(block.personalization), t.begin(), t.size());
        size_t rem = SaturatingSubtract(COUNTOF(block.personalization), t.size());
        size_t off = COUNTOF(block.personalization) - rem;
        if (rem)
            std::memset(block.personalization+off, 0x00, rem);
    }
    else
    {
        std::memset(block.personalization, 0x00, COUNTOF(block.personalization));
    }
}

std::string BLAKE2b::AlgorithmProvider() const
{
#if defined(CRYPTOPP_SSE41_AVAILABLE)
    if (HasSSE41())
        return "SSE4.1";
#endif
#if (CRYPTOPP_ARM_NEON_AVAILABLE)
    if (HasNEON())
        return "NEON";
#endif
#if (CRYPTOPP_POWER8_AVAILABLE)
    if (HasPower8())
        return "Power8";
#endif
    return "C++";
}

std::string BLAKE2s::AlgorithmProvider() const
{
#if defined(CRYPTOPP_SSE41_AVAILABLE)
    if (HasSSE41())
        return "SSE4.1";
#endif
#if (CRYPTOPP_ARM_NEON_AVAILABLE)
    if (HasNEON())
        return "NEON";
#endif
#if (CRYPTOPP_POWER7_AVAILABLE)
    if (HasPower7())
        return "Power7";
#endif
#if (CRYPTOPP_ALTIVEC_AVAILABLE)
    if (HasAltivec())
        return "Altivec";
#endif
    return "C++";
}

BLAKE2s::BLAKE2s(bool treeMode, unsigned int digestSize) : m_state(1), m_block(1), m_digestSize(digestSize), m_treeMode(treeMode)
{
    CRYPTOPP_ASSERT(digestSize <= DIGESTSIZE);

    UncheckedSetKey(NULLPTR, 0, MakeParameters(Name::DigestSize(), (int)digestSize)(Name::TreeMode(), treeMode, false));
    Restart();
}

BLAKE2b::BLAKE2b(bool treeMode, unsigned int digestSize) : m_state(1), m_block(1), m_digestSize(digestSize), m_treeMode(treeMode)
{
    CRYPTOPP_ASSERT(digestSize <= DIGESTSIZE);

    UncheckedSetKey(NULLPTR, 0, MakeParameters(Name::DigestSize(), (int)digestSize)(Name::TreeMode(), treeMode, false));
    Restart();
}

BLAKE2s::BLAKE2s(const byte *key, size_t keyLength, const byte* salt, size_t saltLength,
    const byte* personalization, size_t personalizationLength, bool treeMode, unsigned int digestSize)
    : m_state(1), m_block(1), m_digestSize(digestSize), m_treeMode(treeMode)
{
    CRYPTOPP_ASSERT(keyLength <= MAX_KEYLENGTH);
    CRYPTOPP_ASSERT(digestSize <= DIGESTSIZE);
    CRYPTOPP_ASSERT(saltLength <= SALTSIZE);
    CRYPTOPP_ASSERT(personalizationLength <= PERSONALIZATIONSIZE);

    UncheckedSetKey(key, static_cast<unsigned int>(keyLength), MakeParameters(Name::DigestSize(),(int)digestSize)(Name::TreeMode(),treeMode, false)
        (Name::Salt(), ConstByteArrayParameter(salt, saltLength))(Name::Personalization(), ConstByteArrayParameter(personalization, personalizationLength)));
    Restart();
}

BLAKE2b::BLAKE2b(const byte *key, size_t keyLength, const byte* salt, size_t saltLength,
    const byte* personalization, size_t personalizationLength, bool treeMode, unsigned int digestSize)
    : m_state(1), m_block(1), m_digestSize(digestSize), m_treeMode(treeMode)
{
    CRYPTOPP_ASSERT(keyLength <= MAX_KEYLENGTH);
    CRYPTOPP_ASSERT(digestSize <= DIGESTSIZE);
    CRYPTOPP_ASSERT(saltLength <= SALTSIZE);
    CRYPTOPP_ASSERT(personalizationLength <= PERSONALIZATIONSIZE);

    UncheckedSetKey(key, static_cast<unsigned int>(keyLength), MakeParameters(Name::DigestSize(),(int)digestSize)(Name::TreeMode(),treeMode, false)
        (Name::Salt(), ConstByteArrayParameter(salt, saltLength))(Name::Personalization(), ConstByteArrayParameter(personalization, personalizationLength)));
    Restart();
}

void BLAKE2s::Restart()
{
    static const word32 zero[2] = {0,0};
    Restart(*m_block.data(), zero);
}

void BLAKE2b::Restart()
{
    static const word64 zero[2] = {0,0};
    Restart(*m_block.data(), zero);
}

void BLAKE2s::Restart(const BLAKE2s_ParameterBlock& block, const word32 counter[2])
{
    // We take a parameter block as a parameter to allow customized state.
    // Avoid the copy of the parameter block when we are passing our own block.
    if (&block != m_block.data())
    {
        memcpy_s(m_block.data(), sizeof(ParameterBlock), &block, sizeof(ParameterBlock));
        m_block.data()->digestLength = (byte)m_digestSize;
        m_block.data()->keyLength = (byte)m_key.size();
    }

    State& state = *m_state.data();
    state.tf[0] = state.tf[1] = 0, state.tf[2] = state.tf[3] = 0, state.length = 0;

    if (counter != NULLPTR)
    {
        state.tf[0] = counter[0];
        state.tf[1] = counter[1];
    }

    const word32* iv = BLAKE2S_IV;
    PutBlock<word32, LittleEndian, true> put(m_block.data(), &state.h[0]);
    put(iv[0])(iv[1])(iv[2])(iv[3])(iv[4])(iv[5])(iv[6])(iv[7]);

    // When BLAKE2 is keyed, the input stream is simply {key||message}. Key it
    // during Restart to avoid FirstPut and friends. Key size == 0 means no key.
    if (m_key.size())
        Update(m_key, m_key.size());
}


void BLAKE2b::Restart(const BLAKE2b_ParameterBlock& block, const word64 counter[2])
{
    // We take a parameter block as a parameter to allow customized state.
    // Avoid the copy of the parameter block when we are passing our own block.
    if (&block != m_block.data())
    {
        memcpy_s(m_block.data(), sizeof(ParameterBlock), &block, sizeof(ParameterBlock));
        m_block.data()->digestLength = (byte)m_digestSize;
        m_block.data()->keyLength = (byte)m_key.size();
    }

    State& state = *m_state.data();
    state.tf[0] = state.tf[1] = 0, state.tf[2] = state.tf[3] = 0, state.length = 0;

    if (counter != NULLPTR)
    {
        state.tf[0] = counter[0];
        state.tf[1] = counter[1];
    }

    const word64* iv = BLAKE2B_IV;
    PutBlock<word64, LittleEndian, true> put(m_block.data(), &state.h[0]);
    put(iv[0])(iv[1])(iv[2])(iv[3])(iv[4])(iv[5])(iv[6])(iv[7]);

    // When BLAKE2 is keyed, the input stream is simply {key||message}. Key it
    // during Restart to avoid FirstPut and friends. Key size == 0 means no key.
    if (m_key.size())
        Update(m_key, m_key.size());
}

void BLAKE2s::Update(const byte *input, size_t length)
{
    CRYPTOPP_ASSERT(!(input == NULLPTR && length != 0));
    if (length == 0) { return; }

    State& state = *m_state.data();
    if (state.length + length > BLOCKSIZE)
    {
        // Complete current block
        const size_t fill = BLOCKSIZE - state.length;
        memcpy_s(&state.buffer[state.length], fill, input, fill);

        IncrementCounter();
        Compress(state.buffer);
        state.length = 0;

        length -= fill, input += fill;

        // Compress in-place to avoid copies
        while (length > BLOCKSIZE)
        {
            IncrementCounter();
            Compress(input);
            length -= BLOCKSIZE, input += BLOCKSIZE;
        }
    }

    // Copy tail bytes
    if (input && length)
    {
        CRYPTOPP_ASSERT(length <= BLOCKSIZE - state.length);
        memcpy_s(&state.buffer[state.length], length, input, length);
        state.length += static_cast<unsigned int>(length);
    }
}


void BLAKE2b::Update(const byte *input, size_t length)
{
    CRYPTOPP_ASSERT(!(input == NULLPTR && length != 0));
    if (length == 0) { return; }

    State& state = *m_state.data();
    if (state.length + length > BLOCKSIZE)
    {
        // Complete current block
        const size_t fill = BLOCKSIZE - state.length;
        memcpy_s(&state.buffer[state.length], fill, input, fill);

        IncrementCounter();
        Compress(state.buffer);
        state.length = 0;

        length -= fill, input += fill;

        // Compress in-place to avoid copies
        while (length > BLOCKSIZE)
        {
            IncrementCounter();
            Compress(input);
            length -= BLOCKSIZE, input += BLOCKSIZE;
        }
    }

    // Copy tail bytes
    if (input && length)
    {
        CRYPTOPP_ASSERT(length <= BLOCKSIZE - state.length);
        memcpy_s(&state.buffer[state.length], length, input, length);
        state.length += static_cast<unsigned int>(length);
    }
}

void BLAKE2s::TruncatedFinal(byte *hash, size_t size)
{
    CRYPTOPP_ASSERT(hash != NULLPTR);
    this->ThrowIfInvalidTruncatedSize(size);

    // Set last block unconditionally
    State& state = *m_state.data();
    state.tf[2] = ~static_cast<word32>(0);

    // Set last node if tree mode
    if (m_treeMode)
        state.tf[3] = ~static_cast<word32>(0);

    // Increment counter for tail bytes only
    IncrementCounter(state.length);

    std::memset(state.buffer + state.length, 0x00, BLOCKSIZE - state.length);
    Compress(state.buffer);

    // Copy to caller buffer
    memcpy_s(hash, size, &state.h[0], size);

    Restart();
}

void BLAKE2b::TruncatedFinal(byte *hash, size_t size)
{
    CRYPTOPP_ASSERT(hash != NULLPTR);
    this->ThrowIfInvalidTruncatedSize(size);

    // Set last block unconditionally
    State& state = *m_state.data();
    state.tf[2] = ~static_cast<word64>(0);

    // Set last node if tree mode
    if (m_treeMode)
        state.tf[3] = ~static_cast<word64>(0);

    // Increment counter for tail bytes only
    IncrementCounter(state.length);

    std::memset(state.buffer + state.length, 0x00, BLOCKSIZE - state.length);
    Compress(state.buffer);

    // Copy to caller buffer
    memcpy_s(hash, size, &state.h[0], size);

    Restart();
}

void BLAKE2s::IncrementCounter(size_t count)
{
    State& state = *m_state.data();
    state.tf[0] += static_cast<word32>(count);
    state.tf[1] += !!(state.tf[0] < count);
}

void BLAKE2b::IncrementCounter(size_t count)
{
    State& state = *m_state.data();
    state.tf[0] += static_cast<word64>(count);
    state.tf[1] += !!(state.tf[0] < count);
}

void BLAKE2s::Compress(const byte *input)
{
#if CRYPTOPP_SSE41_AVAILABLE
    if(HasSSE41())
    {
        return BLAKE2_Compress32_SSE4(input, *m_state.data());
    }
#endif
#if CRYPTOPP_ARM_NEON_AVAILABLE
    if(HasNEON())
    {
        return BLAKE2_Compress32_NEON(input, *m_state.data());
    }
#endif
#if CRYPTOPP_ALTIVEC_AVAILABLE
    if(HasAltivec())
    {
        return BLAKE2_Compress32_POWER7(input, *m_state.data());
    }
#endif
    return BLAKE2_Compress32_CXX(input, *m_state.data());
}

void BLAKE2b::Compress(const byte *input)
{
#if CRYPTOPP_SSE41_AVAILABLE
    if(HasSSE41())
    {
        return BLAKE2_Compress64_SSE4(input, *m_state.data());
    }
#endif
#if CRYPTOPP_ARM_NEON_AVAILABLE
    if(HasNEON())
    {
        return BLAKE2_Compress64_NEON(input, *m_state.data());
    }
#endif
#if CRYPTOPP_POWER8_AVAILABLE
    if(HasPower8())
    {
        return BLAKE2_Compress64_POWER8(input, *m_state.data());
    }
#endif
    return BLAKE2_Compress64_CXX(input, *m_state.data());
}

void BLAKE2_Compress64_CXX(const byte* input, BLAKE2b_State& state)
{
    word64 m[16], v[16];

    GetBlock<word64, LittleEndian, true> get1(input);
    get1(m[0])(m[1])(m[2])(m[3])(m[4])(m[5])(m[6])(m[7])(m[8])(m[9])(m[10])(m[11])(m[12])(m[13])(m[14])(m[15]);

    GetBlock<word64, LittleEndian, true> get2(&state.h[0]);
    get2(v[0])(v[1])(v[2])(v[3])(v[4])(v[5])(v[6])(v[7]);

    const word64* iv = BLAKE2B_IV;
    v[ 8] = iv[0];
    v[ 9] = iv[1];
    v[10] = iv[2];
    v[11] = iv[3];
    v[12] = state.tf[0] ^ iv[4];
    v[13] = state.tf[1] ^ iv[5];
    v[14] = state.tf[2] ^ iv[6];
    v[15] = state.tf[3] ^ iv[7];

    BLAKE2B_ROUND<0>(m, v);
    BLAKE2B_ROUND<1>(m, v);
    BLAKE2B_ROUND<2>(m, v);
    BLAKE2B_ROUND<3>(m, v);
    BLAKE2B_ROUND<4>(m, v);
    BLAKE2B_ROUND<5>(m, v);
    BLAKE2B_ROUND<6>(m, v);
    BLAKE2B_ROUND<7>(m, v);
    BLAKE2B_ROUND<8>(m, v);
    BLAKE2B_ROUND<9>(m, v);
    BLAKE2B_ROUND<10>(m, v);
    BLAKE2B_ROUND<11>(m, v);

    for(unsigned int i = 0; i < 8; ++i)
        state.h[i] = state.h[i] ^ ConditionalByteReverse(LittleEndian::ToEnum(), v[i] ^ v[i + 8]);
}

void BLAKE2_Compress32_CXX(const byte* input, BLAKE2s_State& state)
{
    word32 m[16], v[16];

    GetBlock<word32, LittleEndian, true> get1(input);
    get1(m[0])(m[1])(m[2])(m[3])(m[4])(m[5])(m[6])(m[7])(m[8])(m[9])(m[10])(m[11])(m[12])(m[13])(m[14])(m[15]);

    GetBlock<word32, LittleEndian, true> get2(&state.h[0]);
    get2(v[0])(v[1])(v[2])(v[3])(v[4])(v[5])(v[6])(v[7]);

    const word32* iv = BLAKE2S_IV;
    v[ 8] = iv[0];
    v[ 9] = iv[1];
    v[10] = iv[2];
    v[11] = iv[3];
    v[12] = state.tf[0] ^ iv[4];
    v[13] = state.tf[1] ^ iv[5];
    v[14] = state.tf[2] ^ iv[6];
    v[15] = state.tf[3] ^ iv[7];

    BLAKE2S_ROUND<0>(m, v);
    BLAKE2S_ROUND<1>(m, v);
    BLAKE2S_ROUND<2>(m, v);
    BLAKE2S_ROUND<3>(m, v);
    BLAKE2S_ROUND<4>(m, v);
    BLAKE2S_ROUND<5>(m, v);
    BLAKE2S_ROUND<6>(m, v);
    BLAKE2S_ROUND<7>(m, v);
    BLAKE2S_ROUND<8>(m, v);
    BLAKE2S_ROUND<9>(m, v);

    for(unsigned int i = 0; i < 8; ++i)
        state.h[i] = state.h[i] ^ ConditionalByteReverse(LittleEndian::ToEnum(), v[i] ^ v[i + 8]);
}

NAMESPACE_END
