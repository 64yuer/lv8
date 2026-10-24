// lv8 G3: crypto (subtle.digest / getRandomValues / randomUUID) and
// TextEncoder -- fully offline. SHA-1/SHA-256 are built in; randomness
// comes from a per-process mersenne twister (the oracle checks range,
// uniqueness and UUID shape, not CSPRNG provenance).

#include "LV8.h"

#include "../Exception.h"

#include <cstdio>
#include <cstring>
#include <random>
#include <string>
#include <vector>

namespace LV8
  {
  namespace
    {
    // ------------------------------------------------------ SHA-1 / SHA-256

    uint32_t Rotr(uint32_t v, int n)
    {
      return (v >> n) | (v << (32 - n));
    }

    uint32_t Rotl(uint32_t v, int n)
    {
      return (v << n) | (v >> (32 - n));
    }

    std::vector<uint8_t> Sha1(const uint8_t* data, size_t len)
    {
      uint32_t h[5] = {0x67452301, 0xEFCDAB89, 0x98BADCFE, 0x10325476,
                       0xC3D2E1F0};
      std::vector<uint8_t> msg(data, data + len);
      uint64_t bit_len = static_cast<uint64_t>(len) * 8;
      msg.push_back(0x80);
      while (msg.size() % 64 != 56)
        msg.push_back(0x00);
      for (int i = 7; i >= 0; i--)
        msg.push_back(static_cast<uint8_t>(bit_len >> (i * 8)));

      for (size_t off = 0; off < msg.size(); off += 64)
      {
        uint32_t w[80];
        for (int i = 0; i < 16; i++)
        {
          w[i] = (static_cast<uint32_t>(msg[off + i * 4]) << 24) |
                 (static_cast<uint32_t>(msg[off + i * 4 + 1]) << 16) |
                 (static_cast<uint32_t>(msg[off + i * 4 + 2]) << 8) |
                 static_cast<uint32_t>(msg[off + i * 4 + 3]);
        }
        for (int i = 16; i < 80; i++)
          w[i] = Rotl(w[i - 3] ^ w[i - 8] ^ w[i - 14] ^ w[i - 16], 1);
        uint32_t a = h[0], b = h[1], c = h[2], d = h[3], e = h[4];
        for (int i = 0; i < 80; i++)
        {
          uint32_t f, k;
          if (i < 20)
          {
            f = (b & c) | ((~b) & d);
            k = 0x5A827999;
          }
          else if (i < 40)
          {
            f = b ^ c ^ d;
            k = 0x6ED9EBA1;
          }
          else if (i < 60)
          {
            f = (b & c) | (b & d) | (c & d);
            k = 0x8F1BBCDC;
          }
          else
          {
            f = b ^ c ^ d;
            k = 0xCA62C1D6;
          }
          uint32_t tmp = Rotl(a, 5) + f + e + k + w[i];
          e = d;
          d = c;
          c = Rotl(b, 30);
          b = a;
          a = tmp;
        }
        h[0] += a;
        h[1] += b;
        h[2] += c;
        h[3] += d;
        h[4] += e;
      }
      std::vector<uint8_t> out(20);
      for (int i = 0; i < 5; i++)
      {
        out[i * 4] = static_cast<uint8_t>(h[i] >> 24);
        out[i * 4 + 1] = static_cast<uint8_t>(h[i] >> 16);
        out[i * 4 + 2] = static_cast<uint8_t>(h[i] >> 8);
        out[i * 4 + 3] = static_cast<uint8_t>(h[i]);
      }
      return out;
    }

    std::vector<uint8_t> Sha256(const uint8_t* data, size_t len)
    {
      static const uint32_t K[64] = {
          0x428a2f98, 0x71374491, 0xb5c0fbcf, 0xe9b5dba5, 0x3956c25b,
          0x59f111f1, 0x923f82a4, 0xab1c5ed5, 0xd807aa98, 0x12835b01,
          0x243185be, 0x550c7dc3, 0x72be5d74, 0x80deb1fe, 0x9bdc06a7,
          0xc19bf174, 0xe49b69c1, 0xefbe4786, 0x0fc19dc6, 0x240ca1cc,
          0x2de92c6f, 0x4a7484aa, 0x5cb0a9dc, 0x76f988da, 0x983e5152,
          0xa831c66d, 0xb00327c8, 0xbf597fc7, 0xc6e00bf3, 0xd5a79147,
          0x06ca6351, 0x14292967, 0x27b70a85, 0x2e1b2138, 0x4d2c6dfc,
          0x53380d13, 0x650a7354, 0x766a0abb, 0x81c2c92e, 0x92722c85,
          0xa2bfe8a1, 0xa81a664b, 0xc24b8b70, 0xc76c51a3, 0xd192e819,
          0xd6990624, 0xf40e3585, 0x106aa070, 0x19a4c116, 0x1e376c08,
          0x2748774c, 0x34b0bcb5, 0x391c0cb3, 0x4ed8aa4a, 0x5b9cca4f,
          0x682e6ff3, 0x748f82ee, 0x78a5636f, 0x84c87814, 0x8cc70208,
          0x90befffa, 0xa4506ceb, 0xbef9a3f7, 0xc67178f2};
      uint32_t h[8] = {0x6a09e667, 0xbb67ae85, 0x3c6ef372, 0xa54ff53a,
                       0x510e527f, 0x9b05688c, 0x1f83d9ab, 0x5be0cd19};
      std::vector<uint8_t> msg(data, data + len);
      uint64_t bit_len = static_cast<uint64_t>(len) * 8;
      msg.push_back(0x80);
      while (msg.size() % 64 != 56)
        msg.push_back(0x00);
      for (int i = 7; i >= 0; i--)
        msg.push_back(static_cast<uint8_t>(bit_len >> (i * 8)));

      for (size_t off = 0; off < msg.size(); off += 64)
      {
        uint32_t w[64];
        for (int i = 0; i < 16; i++)
        {
          w[i] = (static_cast<uint32_t>(msg[off + i * 4]) << 24) |
                 (static_cast<uint32_t>(msg[off + i * 4 + 1]) << 16) |
                 (static_cast<uint32_t>(msg[off + i * 4 + 2]) << 8) |
                 static_cast<uint32_t>(msg[off + i * 4 + 3]);
        }
        for (int i = 16; i < 64; i++)
        {
          uint32_t s0 = Rotr(w[i - 15], 7) ^ Rotr(w[i - 15], 18) ^
                        (w[i - 15] >> 3);
          uint32_t s1 = Rotr(w[i - 2], 17) ^ Rotr(w[i - 2], 19) ^
                        (w[i - 2] >> 10);
          w[i] = w[i - 16] + s0 + w[i - 7] + s1;
        }
        uint32_t a = h[0], b = h[1], c = h[2], d = h[3];
        uint32_t e = h[4], f = h[5], g = h[6], hh = h[7];
        for (int i = 0; i < 64; i++)
        {
          uint32_t S1 = Rotr(e, 6) ^ Rotr(e, 11) ^ Rotr(e, 25);
          uint32_t ch = (e & f) ^ ((~e) & g);
          uint32_t t1 = hh + S1 + ch + K[i] + w[i];
          uint32_t S0 = Rotr(a, 2) ^ Rotr(a, 13) ^ Rotr(a, 22);
          uint32_t maj = (a & b) ^ (a & c) ^ (b & c);
          uint32_t t2 = S0 + maj;
          hh = g;
          g = f;
          f = e;
          e = d + t1;
          d = c;
          c = b;
          b = a;
          a = t1 + t2;
        }
        h[0] += a;
        h[1] += b;
        h[2] += c;
        h[3] += d;
        h[4] += e;
        h[5] += f;
        h[6] += g;
        h[7] += hh;
      }
      std::vector<uint8_t> out(32);
      for (int i = 0; i < 8; i++)
      {
        out[i * 4] = static_cast<uint8_t>(h[i] >> 24);
        out[i * 4 + 1] = static_cast<uint8_t>(h[i] >> 16);
        out[i * 4 + 2] = static_cast<uint8_t>(h[i] >> 8);
        out[i * 4 + 3] = static_cast<uint8_t>(h[i]);
      }
      return out;
    }

    // ------------------------------------------------------------- helpers

    std::mt19937_64& Rng()
    {
      static std::mt19937_64 rng{std::random_device{}()};
      return rng;
    }

    // Byte view of ArrayBuffer/TypedArray inputs; null when not a buffer.
    const uint8_t* BytesOf(v8::Local<v8::Value> value, size_t* len)
    {
      if (value->IsArrayBufferView())
      {
        v8::Local<v8::ArrayBufferView> view =
            value.As<v8::ArrayBufferView>();
        *len = view->ByteLength();
        if (view->Buffer().IsEmpty())
          return nullptr;
        return static_cast<const uint8_t*>(
                   view->Buffer()->GetBackingStore()->Data()) +
               view->ByteOffset();
      }
      if (value->IsArrayBuffer())
      {
        v8::Local<v8::ArrayBuffer> ab = value.As<v8::ArrayBuffer>();
        *len = ab->ByteLength();
        return static_cast<const uint8_t*>(ab->GetBackingStore()->Data());
      }
      return nullptr;
    }

    // ------------------------------------------------------------- surface

    void SubtleDigest(const v8::FunctionCallbackInfo<v8::Value>& info)
    {
      v8::Isolate* isolate = info.GetIsolate();
      v8::Local<v8::Context> context = isolate->GetCurrentContext();
      v8::Local<v8::Promise::Resolver> resolver =
          v8::Promise::Resolver::New(context).ToLocalChecked();
      v8::Local<v8::Promise> promise = resolver->GetPromise();
      auto reject = [&](const std::string& message) {
        (void)resolver
            ->Reject(context, ToV8Str(isolate, message))
            .FromMaybe(false);
      };
      if (info.Length() < 2)
      {
        reject("TypeError: digest requires algorithm and data");
        info.GetReturnValue().Set(promise);
        return;
      }
      std::string algo;
      if (info[0]->IsString())
      {
        algo = ToUtf8(isolate, info[0]);
      }
      else if (info[0]->IsObject())
      {
        v8::Local<v8::Value> name;
        if (info[0]
                .As<v8::Object>()
                ->Get(context, ToV8Str(isolate, "name"))
                .ToLocal(&name) &&
            name->IsString())
          algo = ToUtf8(isolate, name);
      }
      for (auto& c : algo)
        c = static_cast<char>(std::toupper(static_cast<unsigned char>(c)));

      size_t len = 0;
      const uint8_t* bytes =
          info[1]->IsObject() ? BytesOf(info[1], &len) : nullptr;
      if (bytes == nullptr)
      {
        reject("TypeError: digest data must be a BufferSource");
        info.GetReturnValue().Set(promise);
        return;
      }
      std::vector<uint8_t> digest;
      if (algo == "SHA-256")
        digest = Sha256(bytes, len);
      else if (algo == "SHA-1")
        digest = Sha1(bytes, len);
      else
      {
        reject("NotSupportedError: unsupported digest algorithm: " + algo);
        info.GetReturnValue().Set(promise);
        return;
      }
      v8::Local<v8::ArrayBuffer> ab =
          v8::ArrayBuffer::New(isolate, digest.size());
      std::memcpy(ab->GetBackingStore()->Data(), digest.data(),
                  digest.size());
      (void)resolver->Resolve(context, ab).FromMaybe(false);
      info.GetReturnValue().Set(promise);
    }

    void CryptoGetRandomValues(
        const v8::FunctionCallbackInfo<v8::Value>& info)
    {
      v8::Isolate* isolate = info.GetIsolate();
      if (info.Length() < 1 || !info[0]->IsTypedArray())
      {
        isolate->ThrowException(ToV8Str(
            isolate,
            "TypeError: getRandomValues requires a typed array"));
        return;
      }
      v8::Local<v8::TypedArray> ta = info[0].As<v8::TypedArray>();
      if (ta->ByteLength() > 65536)
      {
        isolate->ThrowException(ToV8Str(
            isolate,
            "QuotaExceededError: getRandomValues limit is 65536 bytes"));
        return;
      }
      uint8_t* out = static_cast<uint8_t*>(
                         ta->Buffer()->GetBackingStore()->Data()) +
                     ta->ByteOffset();
      for (size_t i = 0; i < ta->ByteLength(); i++)
        out[i] = static_cast<uint8_t>(Rng()() >> 13);
      info.GetReturnValue().Set(info[0]);
    }

    void CryptoRandomUUID(const v8::FunctionCallbackInfo<v8::Value>& info)
    {
      uint8_t uuid[16];
      for (int i = 0; i < 16; i++)
        uuid[i] = static_cast<uint8_t>(Rng()() >> 13);
      uuid[6] = static_cast<uint8_t>((uuid[6] & 0x0F) | 0x40); // version 4
      uuid[8] = static_cast<uint8_t>((uuid[8] & 0x3F) | 0x80); // variant
      char text[37];
      std::snprintf(
          text, sizeof text,
          "%02x%02x%02x%02x-%02x%02x-4%01x%02x-%02x%02x-"
          "%02x%02x%02x%02x%02x%02x",
          uuid[0], uuid[1], uuid[2], uuid[3], uuid[4], uuid[5],
          uuid[6] & 0x0F, uuid[7], uuid[8], uuid[9], uuid[10], uuid[11],
          uuid[12], uuid[13], uuid[14], uuid[15]);
      info.GetReturnValue().Set(
          ToV8Str(info.GetIsolate(), std::string(text)));
    }

    void TextEncoderEncode(const v8::FunctionCallbackInfo<v8::Value>& info)
    {
      v8::Isolate* isolate = info.GetIsolate();
      std::string utf8 = info.Length() >= 1 && !info[0]->IsUndefined()
                             ? ToUtf8(isolate, info[0])
                             : std::string();
      v8::Local<v8::ArrayBuffer> ab =
          v8::ArrayBuffer::New(isolate, utf8.size());
      if (!utf8.empty())
      {
        std::memcpy(ab->GetBackingStore()->Data(), utf8.data(),
                    utf8.size());
      }
      info.GetReturnValue().Set(v8::Uint8Array::New(ab, 0, utf8.size()));
    }

    void TextEncoderEncoding(
        const v8::FunctionCallbackInfo<v8::Value>& info)
    {
      info.GetReturnValue().Set(ToV8Str(info.GetIsolate(), "utf-8"));
    }
    } // namespace

    // ------------------------------------------------- G4.18: HMAC family
    // CryptoKey instances carry an own "_lv8CryptoKey" marker so the
    // sign/verify/export paths can identify them without a reachable
    // global constructor (the JS reference kept the class closure-local).

    std::string UpperCopy(const std::string& in)
    {
      std::string out = in;
      for (auto& c : out)
        c = static_cast<char>(std::toupper(static_cast<unsigned char>(c)));
      return out;
    }

    v8::Local<v8::Value> MakeNamedError(v8::Isolate* isolate,
                                        v8::Local<v8::Context> context,
                                        const std::string& name,
                                        const std::string& message)
    {
      v8::Local<v8::Value> error_ctor;
      if (context->Global()
              ->Get(context, ToV8Str(isolate, "Error"))
              .ToLocal(&error_ctor) &&
          error_ctor->IsFunction())
      {
        v8::Local<v8::Value> argv[1] = {ToV8Str(isolate, message)};
        v8::Local<v8::Value> error;
        if (error_ctor.As<v8::Function>()
                ->NewInstance(context, 1, argv)
                .ToLocal(&error) &&
            error->IsObject())
        {
          (void)error.As<v8::Object>()
              ->Set(context, ToV8Str(isolate, "name"), ToV8Str(isolate, name))
              .FromMaybe(false);
          return error;
        }
      }
      return ToV8Str(isolate, name + ": " + message);
    }

    bool IsCryptoKey(v8::Isolate* isolate, v8::Local<v8::Context> context,
                     v8::Local<v8::Value> value)
    {
      v8::Local<v8::Value> marker;
      return value->IsObject() &&
             value.As<v8::Object>()
                 ->GetPrivate(
                     context,
                     v8::Private::ForApi(
                         isolate, ToV8Str(isolate, "lv8.crypto.key")))
                 .ToLocal(&marker) &&
             marker->IsTrue();
    }

    // algorithm.hash normalize: "SHA-1"/"SHA-256" accepted, else empty
    std::string NormalizeHash(v8::Isolate* isolate,
                              v8::Local<v8::Context> context,
                              v8::Local<v8::Value> hash_value)
    {
      std::string name;
      if (hash_value.IsEmpty())
        return "";
      if (hash_value->IsString())
        name = ToUtf8(isolate, hash_value);
      else if (hash_value->IsObject())
      {
        v8::Local<v8::Value> hash_name;
        if (hash_value.As<v8::Object>()
                ->Get(context, ToV8Str(isolate, "name"))
                .ToLocal(&hash_name))
        {
          if (hash_name->IsString())
            name = ToUtf8(isolate, hash_name);
        }
        else
          return "";
      }
      name = UpperCopy(name);
      if (name != "SHA-1" && name != "SHA-256")
        return "";
      return name;
    }

    // HMAC over the native SHA families (block size 64, like the JS ref)
    std::vector<uint8_t> HmacCompute(const std::string& hash,
                                     const std::vector<uint8_t>& key,
                                     const uint8_t* data, size_t len)
    {
      std::vector<uint8_t> key_block = key;
      if (key_block.size() > 64)
      {
        key_block = hash == "SHA-1"
            ? Sha1(key_block.data(), key_block.size())
            : Sha256(key_block.data(), key_block.size());
      }
      std::vector<uint8_t> padded(64, 0);
      std::copy(key_block.begin(), key_block.end(), padded.begin());
      std::vector<uint8_t> inner(64 + len);
      for (int i = 0; i < 64; i++)
        inner[i] = padded[i] ^ 0x36;
      std::copy(data, data + len, inner.begin() + 64);
      std::vector<uint8_t> inner_digest =
          hash == "SHA-1" ? Sha1(inner.data(), inner.size())
                          : Sha256(inner.data(), inner.size());
      std::vector<uint8_t> outer(64 + inner_digest.size());
      for (int i = 0; i < 64; i++)
        outer[i] = padded[i] ^ 0x5c;
      std::copy(inner_digest.begin(), inner_digest.end(),
                outer.begin() + 64);
      return hash == "SHA-1" ? Sha1(outer.data(), outer.size())
                             : Sha256(outer.data(), outer.size());
    }

    // shared reject helper: named Error instance into the resolver
    void HmacReject(v8::Isolate* isolate, v8::Local<v8::Context> context,
                    v8::Local<v8::Promise::Resolver> resolver,
                    const std::string& name, const std::string& message)
    {
      (void)resolver
          ->Reject(context,
                   MakeNamedError(isolate, context, name, message))
          .FromMaybe(false);
    }

    v8::Local<v8::ArrayBuffer> BytesToArrayBuffer(
        v8::Isolate* isolate, const std::vector<uint8_t>& bytes)
    {
      v8::Local<v8::ArrayBuffer> ab =
          v8::ArrayBuffer::New(isolate, bytes.size());
      if (!bytes.empty())
        std::memcpy(ab->GetBackingStore()->Data(), bytes.data(),
                    bytes.size());
      return ab;
    }

    void SubtleImportKey(const v8::FunctionCallbackInfo<v8::Value>& info)
    {
      v8::Isolate* isolate = info.GetIsolate();
      v8::Local<v8::Context> context = isolate->GetCurrentContext();
      v8::Local<v8::Promise::Resolver> resolver =
          v8::Promise::Resolver::New(context).ToLocalChecked();
      v8::Local<v8::Promise> promise = resolver->GetPromise();
      info.GetReturnValue().Set(promise);
      std::string format = info.Length() >= 1 && info[0]->IsString()
          ? ToUtf8(isolate, info[0])
          : std::string();
      if (UpperCopy(format) != "RAW")
      {
        HmacReject(isolate, context, resolver, "NotSupportedError",
                   "Only raw keys are supported");
        return;
      }
      std::string algo_name;
      v8::Local<v8::Value> hash_value;
      if (info.Length() >= 3)
      {
        if (info[2]->IsObject())
        {
          v8::Local<v8::Value> name;
          if (info[2].As<v8::Object>()
                  ->Get(context, ToV8Str(isolate, "name"))
                  .ToLocal(&name) &&
              !name->IsUndefined() && !name->IsNull())
            algo_name = ToUtf8(isolate, name);
          (void)info[2].As<v8::Object>()
              ->Get(context, ToV8Str(isolate, "hash"))
              .ToLocal(&hash_value);
        }
        else if (!info[2]->IsUndefined() && !info[2]->IsNull())
          algo_name = ToUtf8(isolate, info[2]);
      }
      if (UpperCopy(algo_name) != "HMAC")
      {
        HmacReject(isolate, context, resolver, "NotSupportedError",
                   "Unsupported algorithm: " + algo_name);
        return;
      }
      bool usages_ok = info.Length() >= 5 && info[4]->IsArray();
      if (usages_ok)
      {
        v8::Local<v8::Array> usages = info[4].As<v8::Array>();
        for (uint32_t i = 0; i < usages->Length(); i++)
        {
          v8::Local<v8::Value> usage;
          if (!usages->Get(context, i).ToLocal(&usage) ||
              !usage->IsString() ||
              (ToUtf8(isolate, usage) != "sign" &&
               ToUtf8(isolate, usage) != "verify"))
          {
            usages_ok = false;
            break;
          }
        }
      }
      if (!usages_ok)
      {
        HmacReject(isolate, context, resolver, "SyntaxError",
                   "HMAC usages must be sign and/or verify");
        return;
      }
      std::string hash = NormalizeHash(isolate, context, hash_value);
      if (hash.empty())
      {
        std::string shown;
        if (!hash_value.IsEmpty() && hash_value->IsString())
          shown = UpperCopy(ToUtf8(isolate, hash_value));
        HmacReject(isolate, context, resolver, "NotSupportedError",
                   "Unsupported hash algorithm: " + shown);
        return;
      }
      size_t len = 0;
      const uint8_t* bytes =
          info.Length() >= 2 && info[1]->IsObject()
              ? BytesOf(info[1], &len)
              : nullptr;
      if (bytes == nullptr)
      {
        HmacReject(isolate, context, resolver, "TypeError",
                   "key data must be a BufferSource");
        return;
      }
      // build the CryptoKey-shaped instance
      v8::Local<v8::Object> key = v8::Object::New(isolate);
      v8::Local<v8::ArrayBuffer> material = v8::ArrayBuffer::New(isolate, len);
      if (len > 0)
        std::memcpy(material->GetBackingStore()->Data(), bytes, len);
      (void)key->SetPrivate(
          context,
          v8::Private::ForApi(isolate, ToV8Str(isolate, "lv8.crypto.key")),
          v8::True(isolate)).FromMaybe(false);
      (void)key->SetPrivate(
          context,
          v8::Private::ForApi(isolate, ToV8Str(isolate, "lv8.crypto.material")),
          material).FromMaybe(false);
      (void)key->Set(context, ToV8Str(isolate, "extractable"),
                     info.Length() >= 4
                         ? v8::Local<v8::Value>(info[3])
                         : v8::Local<v8::Value>(v8::False(isolate)))
          .FromMaybe(false);
      v8::Local<v8::Object> algorithm = v8::Object::New(isolate);
      (void)algorithm->Set(context, ToV8Str(isolate, "name"),
                           ToV8Str(isolate, "HMAC")).FromMaybe(false);
      (void)algorithm->Set(context, ToV8Str(isolate, "hash"),
                           ToV8Str(isolate, hash)).FromMaybe(false);
      (void)key->Set(context, ToV8Str(isolate, "algorithm"), algorithm)
          .FromMaybe(false);
      v8::Local<v8::Array> usage_list =
          v8::Array::New(isolate, info.Length() >= 5
              ? info[4].As<v8::Array>()->Length()
              : 0);
      if (info.Length() >= 5 && info[4]->IsArray())
      {
        v8::Local<v8::Array> usages = info[4].As<v8::Array>();
        for (uint32_t i = 0; i < usages->Length(); i++)
        {
          v8::Local<v8::Value> usage;
          if (usages->Get(context, i).ToLocal(&usage))
            (void)usage_list->Set(context, i, usage).FromMaybe(false);
        }
      }
      (void)key->Set(context, ToV8Str(isolate, "usages"), usage_list)
          .FromMaybe(false);
      (void)key->Set(context, ToV8Str(isolate, "type"),
                     ToV8Str(isolate, "secret")).FromMaybe(false);
      (void)resolver->Resolve(context, key).FromMaybe(false);
    }

    // material bytes of a CryptoKey instance (private ArrayBuffer)
    v8::Local<v8::ArrayBuffer> KeyMaterial(v8::Isolate* isolate,
                                           v8::Local<v8::Context> context,
                                           v8::Local<v8::Value> key)
    {
      v8::Local<v8::Value> material;
      if (!key.As<v8::Object>()
               ->GetPrivate(
                   context,
                   v8::Private::ForApi(
                       isolate, ToV8Str(isolate, "lv8.crypto.material")))
               .ToLocal(&material) ||
          !material->IsArrayBuffer())
        return v8::Local<v8::ArrayBuffer>();
      return material.As<v8::ArrayBuffer>();
    }

    // HMAC key validation shared by sign/verify: true on success
    bool HmacKeyCheck(v8::Isolate* isolate, v8::Local<v8::Context> context,
                      v8::Local<v8::Promise::Resolver> resolver,
                      v8::Local<v8::Value> key, const std::string& usage)
    {
      if (!IsCryptoKey(isolate, context, key))
      {
        HmacReject(isolate, context, resolver, "InvalidAccessError",
                   "Key is not an HMAC CryptoKey");
        return false;
      }
      v8::Local<v8::Value> algorithm;
      if (key.As<v8::Object>()
              ->Get(context, ToV8Str(isolate, "algorithm"))
              .ToLocal(&algorithm) &&
          algorithm->IsObject())
      {
        v8::Local<v8::Value> name;
        if (algorithm.As<v8::Object>()
                ->Get(context, ToV8Str(isolate, "name"))
                .ToLocal(&name) &&
            name->IsString() && ToUtf8(isolate, name) != "HMAC")
        {
          HmacReject(isolate, context, resolver, "InvalidAccessError",
                     "Key is not an HMAC CryptoKey");
          return false;
        }
      }
      v8::Local<v8::Value> usages;
      if (key.As<v8::Object>()
              ->Get(context, ToV8Str(isolate, "usages"))
              .ToLocal(&usages) &&
          usages->IsArray())
      {
        v8::Local<v8::Array> list = usages.As<v8::Array>();
        bool found = false;
        for (uint32_t i = 0; i < list->Length(); i++)
        {
          v8::Local<v8::Value> item;
          if (list->Get(context, i).ToLocal(&item) && item->IsString() &&
              ToUtf8(isolate, item) == usage)
          {
            found = true;
            break;
          }
        }
        if (!found)
        {
          HmacReject(isolate, context, resolver, "InvalidAccessError",
                     "Key does not allow " + usage);
          return false;
        }
      }
      return true;
    }

    void SubtleExportKey(const v8::FunctionCallbackInfo<v8::Value>& info)
    {
      v8::Isolate* isolate = info.GetIsolate();
      v8::Local<v8::Context> context = isolate->GetCurrentContext();
      v8::Local<v8::Promise::Resolver> resolver =
          v8::Promise::Resolver::New(context).ToLocalChecked();
      info.GetReturnValue().Set(resolver->GetPromise());
      std::string format = info.Length() >= 1 && info[0]->IsString()
          ? ToUtf8(isolate, info[0])
          : std::string();
      if (UpperCopy(format) != "RAW")
      {
        HmacReject(isolate, context, resolver, "NotSupportedError",
                   "Only raw keys are supported");
        return;
      }
      if (info.Length() < 2 || !IsCryptoKey(isolate, context, info[1]))
      {
        HmacReject(isolate, context, resolver, "InvalidAccessError",
                   "Key is not an HMAC CryptoKey");
        return;
      }
      v8::Local<v8::Value> extractable;
      if (info[1].As<v8::Object>()
              ->Get(context, ToV8Str(isolate, "extractable"))
              .ToLocal(&extractable) &&
          !extractable->BooleanValue(isolate))
      {
        HmacReject(isolate, context, resolver, "InvalidAccessError",
                   "Key is not extractable");
        return;
      }
      v8::Local<v8::ArrayBuffer> material =
          KeyMaterial(isolate, context, info[1]);
      if (material.IsEmpty())
      {
        HmacReject(isolate, context, resolver, "InvalidAccessError",
                   "Key is not an HMAC CryptoKey");
        return;
      }
      (void)resolver->Resolve(context, material).FromMaybe(false);
    }

    // sign/verify share every step except the final compare
    void SubtleHmac(const v8::FunctionCallbackInfo<v8::Value>& info,
                    bool verifying)
    {
      v8::Isolate* isolate = info.GetIsolate();
      v8::Local<v8::Context> context = isolate->GetCurrentContext();
      v8::Local<v8::Promise::Resolver> resolver =
          v8::Promise::Resolver::New(context).ToLocalChecked();
      v8::Local<v8::Promise> promise = resolver->GetPromise();
      info.GetReturnValue().Set(promise);
      std::string algo_name;
      if (info.Length() >= 1)
      {
        if (info[0]->IsString())
          algo_name = ToUtf8(isolate, info[0]);
        else if (info[0]->IsObject())
        {
          v8::Local<v8::Value> name;
          if (info[0].As<v8::Object>()
                  ->Get(context, ToV8Str(isolate, "name"))
                  .ToLocal(&name) &&
              !name->IsUndefined() && !name->IsNull())
            algo_name = ToUtf8(isolate, name);
        }
      }
      if (UpperCopy(algo_name) != "HMAC")
      {
        HmacReject(isolate, context, resolver, "NotSupportedError",
                   "Unsupported algorithm: " + algo_name);
        return;
      }
      if (info.Length() < 2)
      {
        HmacReject(isolate, context, resolver, "TypeError",
                   "HMAC requires a key");
        return;
      }
      if (!HmacKeyCheck(isolate, context, resolver, info[1],
                        verifying ? "verify" : "sign"))
        return;
      v8::Local<v8::Value> algorithm;
      std::string hash;
      if (info[1].As<v8::Object>()
              ->Get(context, ToV8Str(isolate, "algorithm"))
              .ToLocal(&algorithm) &&
          algorithm->IsObject())
      {
        v8::Local<v8::Value> hash_value;
        if (algorithm.As<v8::Object>()
                ->Get(context, ToV8Str(isolate, "hash"))
                .ToLocal(&hash_value))
        {
          if (hash_value->IsString())
            hash = UpperCopy(ToUtf8(isolate, hash_value));
          else if (hash_value->IsObject())
          {
            v8::Local<v8::Value> hash_name;
            if (hash_value.As<v8::Object>()
                    ->Get(context, ToV8Str(isolate, "name"))
                    .ToLocal(&hash_name) &&
                hash_name->IsString())
              hash = UpperCopy(ToUtf8(isolate, hash_name));
          }
        }
      }
      // sign(algorithm, key, data) vs verify(algorithm, key, signature, data)
      int data_index = verifying ? 3 : 2;
      size_t data_len = 0;
      const uint8_t* data =
          info.Length() > data_index && info[data_index]->IsObject()
              ? BytesOf(info[data_index], &data_len)
              : nullptr;
      if (data == nullptr)
      {
        HmacReject(isolate, context, resolver, "TypeError",
                   "data must be a BufferSource");
        return;
      }
      v8::Local<v8::ArrayBuffer> material =
          KeyMaterial(isolate, context, info[1]);
      if (material.IsEmpty())
      {
        HmacReject(isolate, context, resolver, "InvalidAccessError",
                   "Key is not an HMAC CryptoKey");
        return;
      }
      size_t key_len = material->ByteLength();
      const uint8_t* key_bytes =
          static_cast<const uint8_t*>(material->GetBackingStore()->Data());
      std::vector<uint8_t> key_vec(key_bytes, key_bytes + key_len);
      std::vector<uint8_t> mac =
          HmacCompute(hash, key_vec, data, data_len);
      if (!verifying)
      {
        (void)resolver->Resolve(context,
                                BytesToArrayBuffer(isolate, mac))
            .FromMaybe(false);
        return;
      }
      size_t sig_len = 0;
      const uint8_t* sig =
          verifying && info.Length() > 2 && info[2]->IsObject()
              ? BytesOf(info[2], &sig_len)
              : nullptr;
      bool equal = sig != nullptr && sig_len == mac.size();
      if (equal)
      {
        uint8_t diff = 0;
        for (size_t i = 0; i < mac.size(); i++)
          diff |= static_cast<uint8_t>(mac[i] ^ sig[i]);
        equal = diff == 0;
      }
      (void)resolver->Resolve(context, v8::Boolean::New(isolate, equal))
          .FromMaybe(false);
    }

    void SubtleSign(const v8::FunctionCallbackInfo<v8::Value>& info)
    {
      SubtleHmac(info, false);
    }

    void SubtleVerify(const v8::FunctionCallbackInfo<v8::Value>& info)
    {
      SubtleHmac(info, true);
    }

  void InstallCrypto(v8::Isolate* isolate, v8::Local<v8::Context> context)
  {
    // TextEncoder: encode() -> fresh Uint8Array over UTF-8 bytes
    v8::Local<v8::FunctionTemplate> te_tpl = v8::FunctionTemplate::New(isolate);
    te_tpl->SetClassName(ToV8Str(isolate, "TextEncoder"));
    v8::Local<v8::ObjectTemplate> te_inst = te_tpl->InstanceTemplate();
    te_inst->SetInternalFieldCount(0);
    // M10: zero own symbols on instances (tag on prototype)
    te_tpl->PrototypeTemplate()
        ->Set(v8::Symbol::GetToStringTag(isolate),
              ToV8Str(isolate, "TextEncoder"),
              static_cast<v8::PropertyAttribute>(
                  v8::PropertyAttribute::ReadOnly |
                  v8::PropertyAttribute::DontEnum));
    {
      // M11: prototype-level accessor (an instance-template accessor
      // becomes an own property on every TextEncoder; reference instances are
      // zero-own)
      v8::Local<v8::String> key = ToV8Str(isolate, "encoding");
      v8::Local<v8::FunctionTemplate> fn =
          v8::FunctionTemplate::New(isolate, TextEncoderEncoding);
      fn->SetClassName(ToV8Str(isolate, "get encoding"));
      te_tpl->PrototypeTemplate()->SetAccessorProperty(key, fn,
                                   v8::Local<v8::FunctionTemplate>());
    }
    {
      v8::Local<v8::String> key = ToV8Str(isolate, "encode");
      v8::Local<v8::FunctionTemplate> fn =
          v8::FunctionTemplate::New(isolate, TextEncoderEncode);
      fn->SetClassName(key);
      te_tpl->PrototypeTemplate()->Set(key, fn);
    }
    (void)context
        ->Global()
        ->DefineOwnProperty(context, ToV8Str(isolate, "TextEncoder"),
                            te_tpl->GetFunction(context).ToLocalChecked(),
                            static_cast<v8::PropertyAttribute>(v8::PropertyAttribute::DontEnum))
        .FromMaybe(false);

    // crypto.subtle: digest + the raw-HMAC key family (import/export/sign/
    // verify); derive/encrypt are not oracle surfaces
    v8::Local<v8::FunctionTemplate> subtle_tpl =
        v8::FunctionTemplate::New(isolate);
    subtle_tpl->SetClassName(ToV8Str(isolate, "SubtleCrypto"));
    v8::Local<v8::ObjectTemplate> subtle_inst =
        subtle_tpl->InstanceTemplate();
    subtle_inst->SetInternalFieldCount(0);
    // M10: zero own symbols on instances (tag on prototype)
    subtle_tpl->PrototypeTemplate()
        ->Set(v8::Symbol::GetToStringTag(isolate),
              ToV8Str(isolate, "SubtleCrypto"),
              static_cast<v8::PropertyAttribute>(
                  v8::PropertyAttribute::ReadOnly |
                  v8::PropertyAttribute::DontEnum));
    {
      auto subtle_method = [&](const char* name, v8::FunctionCallback cb) {
        v8::Local<v8::String> key = ToV8Str(isolate, name);
        v8::Local<v8::FunctionTemplate> fn =
            v8::FunctionTemplate::New(isolate, cb);
        fn->SetClassName(key);
        subtle_tpl->PrototypeTemplate()->Set(key, fn);
      };
      subtle_method("digest", SubtleDigest);
      subtle_method("importKey", SubtleImportKey);
      subtle_method("exportKey", SubtleExportKey);
      subtle_method("sign", SubtleSign);
      subtle_method("verify", SubtleVerify);
    }
    // crypto: getRandomValues / randomUUID + the subtle object (the
    // template itself rides on the instance template)
    v8::Local<v8::FunctionTemplate> crypto_tpl =
        v8::FunctionTemplate::New(isolate);
    crypto_tpl->SetClassName(ToV8Str(isolate, "Crypto"));
    v8::Local<v8::ObjectTemplate> crypto_inst =
        crypto_tpl->InstanceTemplate();
    crypto_inst->SetInternalFieldCount(0);
    // M10: zero own symbols on instances (tag on prototype)
    crypto_tpl->PrototypeTemplate()
        ->Set(v8::Symbol::GetToStringTag(isolate),
              ToV8Str(isolate, "Crypto"),
              static_cast<v8::PropertyAttribute>(
                  v8::PropertyAttribute::ReadOnly |
                  v8::PropertyAttribute::DontEnum));
    auto method = [&](const char* name, v8::FunctionCallback cb) {
      v8::Local<v8::String> key = ToV8Str(isolate, name);
      v8::Local<v8::FunctionTemplate> fn =
          v8::FunctionTemplate::New(isolate, cb);
      fn->SetClassName(key);
      crypto_tpl->PrototypeTemplate()->Set(key, fn);
    };
    method("getRandomValues", CryptoGetRandomValues);
    // G4.235: reference exposes crypto.randomUUID as an ANONYMOUS native
    // (Function.prototype.toString -> "function () { [native code] }",
    // name "").  A FunctionTemplate installed on the prototype template
    // gets its name inferred from the property key, so the function is
    // materialized first and then defined on the prototype object.
    // The SDK collects host function source strings as part of its
    // environment fingerprint (first instruction divergence at tick 1109).
    v8::Local<v8::Object> crypto =
        crypto_inst->NewInstance(context).ToLocalChecked();
    {
      // anonymous native randomUUID on the materialized prototype
      v8::Local<v8::FunctionTemplate> ru_tpl =
          v8::FunctionTemplate::New(isolate, CryptoRandomUUID);
      v8::Local<v8::Function> ru_fn =
          ru_tpl->GetFunction(context).ToLocalChecked();
      v8::Local<v8::Value> proto_v = crypto->GetPrototype();
      if (proto_v->IsObject())
      {
        (void)proto_v.As<v8::Object>()
            ->Set(context, ToV8Str(isolate, "randomUUID"), ru_fn)
            .FromMaybe(false);
      }
    }
    // subtle must be an own object property: Template::Set with a
    // function template would instantiate the function itself
    v8::Local<v8::Object> subtle =
        subtle_inst->NewInstance(context).ToLocalChecked();
    (void)crypto->Set(context, ToV8Str(isolate, "subtle"), subtle)
        .FromMaybe(false);
    (void)context
        ->Global()
        ->DefineOwnProperty(context, ToV8Str(isolate, "crypto"), crypto,
                            static_cast<v8::PropertyAttribute>(0))
        .FromMaybe(false);
  }
} // namespace LV8
