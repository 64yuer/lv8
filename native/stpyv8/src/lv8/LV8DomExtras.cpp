// lv8 G3: Node constants, trustedTypes, DOMException, and specialized
// element URL properties. All are deterministic offline surfaces.

#include "LV8.h"

#include "../Exception.h"

#include <array>
#include <cmath>
#include <cstring>
#include <limits>
#include <sstream>
#include <string>

namespace LV8
  {
  // ------------------------------------------------- G4 ledger chunk 1:
  // geometry (DOMRect/DOMPoint/DOMMatrix/DOMQuad) and the DataTransfer
  // family, previously JS bootstrap classes (polyfills2). Constructor
  // templates double as shape templates; state lives in private slots.

  namespace
  {
    v8::Local<v8::Private> DtDataKey(v8::Isolate* isolate)
    {
      return v8::Private::ForApi(isolate,
                                 ToV8Str(isolate, "lv8.dt.data"));
    }
    v8::Local<v8::Private> DtFilesKey(v8::Isolate* isolate)
    {
      return v8::Private::ForApi(isolate,
                                 ToV8Str(isolate, "lv8.dt.files"));
    }
    v8::Local<v8::Private> DtilOwnerKey(v8::Isolate* isolate)
    {
      return v8::Private::ForApi(isolate,
                                 ToV8Str(isolate, "lv8.dtil.owner"));
    }
    v8::Local<v8::Private> DtiValueKey(v8::Isolate* isolate)
    {
      return v8::Private::ForApi(isolate,
                                 ToV8Str(isolate, "lv8.dti.value"));
    }

    // byte range of an ArrayBuffer (GetBackingStore is the supported
    // accessor in this V8 version; LV8Net.cpp carries an identical copy)
    const uint8_t* ArrayBufferBytes(v8::Local<v8::ArrayBuffer> bytes,
                                    size_t* length)
    {
      *length = 0;
      if (bytes.IsEmpty()) return nullptr;
      auto store = bytes->GetBackingStore();
      if (!store || !store->Data()) return nullptr;
      *length = store->ByteLength();
      return static_cast<const uint8_t*>(store->Data());
    }

    double NumOr(v8::Local<v8::Context> context,
                 v8::Local<v8::Value> value, double fallback)
    {
      if (value.IsEmpty()) return fallback;
      return value->NumberValue(context).FromMaybe(fallback);
    }

    void SetNumberProps(v8::Isolate* isolate, v8::Local<v8::Context> context,
                        v8::Local<v8::Object> obj,
                        const char* const names[], const double values[],
                        int count)
    {
      for (int i = 0; i < count; i++)
        (void)obj->Set(context, ToV8Str(isolate, names[i]),
                       v8::Number::New(isolate, values[i])).FromMaybe(false);
    }

    // ---- DOMRect ----------------------------------------------------

    v8::Local<v8::Object> MakeRect(v8::Isolate* isolate,
                                   v8::Local<v8::Context> context,
                                   double x, double y, double width,
                                   double height)
    {
      // reference parity: rect values live in internal fields (invisible to
      // getOwnPropertyNames - the reference instance own-face is empty)
      ContextState* state = GetState(context);
      v8::Local<v8::Object> inst = state->domrect_tpl.Get(isolate)
                                       ->InstanceTemplate()
                                       ->NewInstance(context)
                                       .ToLocalChecked();
      const double values[] = {x, y, width, height};
      for (int i = 0; i < 4; i++)
        inst->SetInternalField(
            i, v8::Number::New(isolate, values[i]));
      return inst;
    }

    double RectInternal(const v8::FunctionCallbackInfo<v8::Value>& info,
                        int field)
    {
      v8::Isolate* isolate = info.GetIsolate();
      v8::Local<v8::Context> context = isolate->GetCurrentContext();
      v8::Local<v8::Object> self = info.This();
      if (self->InternalFieldCount() > field)
      {
        v8::Local<v8::Value> v =
            v8::Local<v8::Value>::Cast(self->GetInternalField(field));
        if (v->IsNumber())
          return v.As<v8::Number>()->Value();
      }
      return NumOr(context,
                   self->Get(context, ToV8Str(isolate, "x"))
                       .FromMaybe(v8::Local<v8::Value>()),
                   0.0);
    }
    void RectX(const v8::FunctionCallbackInfo<v8::Value>& info)
    {
      info.GetReturnValue().Set(
          v8::Number::New(info.GetIsolate(), RectInternal(info, 0)));
    }
    void RectY(const v8::FunctionCallbackInfo<v8::Value>& info)
    {
      info.GetReturnValue().Set(
          v8::Number::New(info.GetIsolate(), RectInternal(info, 1)));
    }
    void RectWidth(const v8::FunctionCallbackInfo<v8::Value>& info)
    {
      info.GetReturnValue().Set(
          v8::Number::New(info.GetIsolate(), RectInternal(info, 2)));
    }
    void RectHeight(const v8::FunctionCallbackInfo<v8::Value>& info)
    {
      info.GetReturnValue().Set(
          v8::Number::New(info.GetIsolate(), RectInternal(info, 3)));
    }

    void DomRectConstructor(const v8::FunctionCallbackInfo<v8::Value>& info)
    {
      v8::Isolate* isolate = info.GetIsolate();
      v8::Local<v8::Context> context = isolate->GetCurrentContext();
      if (!info.IsConstructCall())
      {
        isolate->ThrowException(
            ToV8Str(isolate, "TypeError: DOMRect constructor requires new"));
        return;
      }
      double v[4] = {0, 0, 0, 0};
      for (int i = 0; i < 4 && i < info.Length(); i++)
        v[i] = NumOr(context, info[i], 0.0);
      info.GetReturnValue().Set(
          MakeRect(isolate, context, v[0], v[1], v[2], v[3]));
    }

    // rect edge getter shared by left/right/top/bottom
    void RectEdge(const v8::FunctionCallbackInfo<v8::Value>& info, int kind)
    {
      v8::Isolate* isolate = info.GetIsolate();
      double x = RectInternal(info, 0);
      double y = RectInternal(info, 1);
      double w = RectInternal(info, 2);
      double h = RectInternal(info, 3);
      double value = 0.0;
      if (kind == 0) value = std::min(x, x + w);        // left
      else if (kind == 1) value = std::max(x, x + w);   // right
      else if (kind == 2) value = std::min(y, y + h);   // top
      else value = std::max(y, y + h);                  // bottom
      info.GetReturnValue().Set(v8::Number::New(isolate, value));
    }
    void RectLeft(const v8::FunctionCallbackInfo<v8::Value>& info)
    { RectEdge(info, 0); }
    void RectRight(const v8::FunctionCallbackInfo<v8::Value>& info)
    { RectEdge(info, 1); }
    void RectTop(const v8::FunctionCallbackInfo<v8::Value>& info)
    { RectEdge(info, 2); }
    void RectBottom(const v8::FunctionCallbackInfo<v8::Value>& info)
    { RectEdge(info, 3); }

    void RectToJSON(const v8::FunctionCallbackInfo<v8::Value>& info)
    {
      v8::Isolate* isolate = info.GetIsolate();
      v8::Local<v8::Context> context = isolate->GetCurrentContext();
      double x = RectInternal(info, 0);
      double y = RectInternal(info, 1);
      double w = RectInternal(info, 2);
      double h = RectInternal(info, 3);
      v8::Local<v8::Object> out = v8::Object::New(isolate);
      const char* names[] = {"x", "y", "width", "height"};
      const double values[] = {x, y, w, h};
      SetNumberProps(isolate, context, out, names, values, 4);
      (void)out->Set(context, ToV8Str(isolate, "top"),
                     v8::Number::New(isolate, std::min(y, y + h)))
          .FromMaybe(false);
      (void)out->Set(context, ToV8Str(isolate, "right"),
                     v8::Number::New(isolate, std::max(x, x + w)))
          .FromMaybe(false);
      (void)out->Set(context, ToV8Str(isolate, "bottom"),
                     v8::Number::New(isolate, std::max(y, y + h)))
          .FromMaybe(false);
      (void)out->Set(context, ToV8Str(isolate, "left"),
                     v8::Number::New(isolate, std::min(x, x + w)))
          .FromMaybe(false);
      info.GetReturnValue().Set(out);
    }

    void RectFromRect(const v8::FunctionCallbackInfo<v8::Value>& info)
    {
      v8::Isolate* isolate = info.GetIsolate();
      v8::Local<v8::Context> context = isolate->GetCurrentContext();
      double v[4] = {0, 0, 0, 0};
      if (info.Length() >= 1 && info[0]->IsObject())
      {
        const char* names[] = {"x", "y", "width", "height"};
        for (int i = 0; i < 4; i++)
          v[i] = NumOr(context,
                       info[0].As<v8::Object>()
                           ->Get(context, ToV8Str(isolate, names[i]))
                           .FromMaybe(v8::Local<v8::Value>()),
                       0.0);
      }
      info.GetReturnValue().Set(
          MakeRect(isolate, context, v[0], v[1], v[2], v[3]));
    }

    // ---- DOMPoint / DOMPointReadOnly ---------------------------------

    v8::Local<v8::Object> MakePoint(v8::Isolate* isolate,
                                    v8::Local<v8::Context> context,
                                    bool mutable_point, double x, double y,
                                    double z, double w)
    {
      ContextState* state = GetState(context);
      v8::Local<v8::FunctionTemplate> shape =
          mutable_point && !state->dompoint_tpl.IsEmpty()
              ? state->dompoint_tpl.Get(isolate)
              : state->dompointro_tpl.Get(isolate);
      v8::Local<v8::Object> inst = shape->InstanceTemplate()
                                       ->NewInstance(context)
                                       .ToLocalChecked();
      // M12: coordinates live in internal fields 0..3 (reference instances are
      // zero-own; same internal-field storage as MakeRect) - own _x.._w
      // data props would pollute the own-face the WAF enumerates
      const double values[] = {x, y, z, w};
      for (int i = 0; i < 4; i++)
        inst->SetInternalField(i, v8::Number::New(isolate, values[i]));
      return inst;
    }

    // M12: "_x"/"_y"/"_z"/"_w" -> internal field index 0..3
    int PointFieldIndex(const char* field)
    {
      switch (field[1])
      {
        case 'x': return 0;
        case 'y': return 1;
        case 'z': return 2;
        default: return 3;
      }
    }

    void PointComponent(const v8::FunctionCallbackInfo<v8::Value>& info,
                        const char* field)
    {
      v8::Isolate* isolate = info.GetIsolate();
      v8::Local<v8::Object> self = info.This();
      int idx = PointFieldIndex(field);
      if (self->InternalFieldCount() > idx)
      {
        info.GetReturnValue().Set(v8::Local<v8::Value>::Cast(self->GetInternalField(idx)));
        return;
      }
      info.GetReturnValue().Set(v8::Number::New(isolate, 0.0));
    }
    void PointGetX(const v8::FunctionCallbackInfo<v8::Value>& info)
    { PointComponent(info, "_x"); }
    void PointGetY(const v8::FunctionCallbackInfo<v8::Value>& info)
    { PointComponent(info, "_y"); }
    void PointGetZ(const v8::FunctionCallbackInfo<v8::Value>& info)
    { PointComponent(info, "_z"); }
    void PointGetW(const v8::FunctionCallbackInfo<v8::Value>& info)
    { PointComponent(info, "_w"); }

    void PointSetComponent(const v8::FunctionCallbackInfo<v8::Value>& info,
                           const char* field)
    {
      v8::Isolate* isolate = info.GetIsolate();
      if (info.Length() < 1) return;
      v8::Local<v8::Object> self = info.This();
      int idx = PointFieldIndex(field);
      if (self->InternalFieldCount() > idx)
        self->SetInternalField(
            idx, v8::Number::New(isolate, NumOr(info.GetIsolate()
                                                    ->GetCurrentContext(),
                                                info[0], 0.0)));
    }
    void PointSetX(const v8::FunctionCallbackInfo<v8::Value>& info)
    { PointSetComponent(info, "_x"); }
    void PointSetY(const v8::FunctionCallbackInfo<v8::Value>& info)
    { PointSetComponent(info, "_y"); }
    void PointSetZ(const v8::FunctionCallbackInfo<v8::Value>& info)
    { PointSetComponent(info, "_z"); }
    void PointSetW(const v8::FunctionCallbackInfo<v8::Value>& info)
    { PointSetComponent(info, "_w"); }

    void PointReadOnlyConstructor(
        const v8::FunctionCallbackInfo<v8::Value>& info)
    {
      v8::Isolate* isolate = info.GetIsolate();
      v8::Local<v8::Context> context = isolate->GetCurrentContext();
      if (!info.IsConstructCall())
      {
        isolate->ThrowException(ToV8Str(
            isolate,
            "TypeError: DOMPointReadOnly constructor requires new"));
        return;
      }
      double v[4] = {0, 0, 0, 1};
      for (int i = 0; i < 4 && i < info.Length(); i++)
        v[i] = NumOr(context, info[i], i == 3 ? 1.0 : 0.0);
      info.GetReturnValue().Set(
          MakePoint(isolate, context, false, v[0], v[1], v[2], v[3]));
    }

    void PointConstructor(const v8::FunctionCallbackInfo<v8::Value>& info)
    {
      v8::Isolate* isolate = info.GetIsolate();
      v8::Local<v8::Context> context = isolate->GetCurrentContext();
      if (!info.IsConstructCall())
      {
        isolate->ThrowException(ToV8Str(
            isolate, "TypeError: DOMPoint constructor requires new"));
        return;
      }
      double v[4] = {0, 0, 0, 1};
      for (int i = 0; i < 4 && i < info.Length(); i++)
        v[i] = NumOr(context, info[i], i == 3 ? 1.0 : 0.0);
      info.GetReturnValue().Set(
          MakePoint(isolate, context, true, v[0], v[1], v[2], v[3]));
    }

    // reads x/y/z/w off any point-like object (defaults per reference)
    void ReadPointLike(v8::Isolate* isolate, v8::Local<v8::Context> context,
                       v8::Local<v8::Value> value, double out[4])
    {
      out[0] = 0; out[1] = 0; out[2] = 0; out[3] = 1;
      // M12: absent args arrive as empty Locals from some callers - a bare
      // IsObject() on an empty handle derefs null (0xC0000005, the DOMQuad
      // zero-arg chunk crash)
      if (value.IsEmpty() || !value->IsObject()) return;
      const char* names[] = {"x", "y", "z", "w"};
      for (int i = 0; i < 4; i++)
      {
        v8::Local<v8::Value> raw;
        if (value.As<v8::Object>()
                ->Get(context, ToV8Str(isolate, names[i]))
                .ToLocal(&raw) &&
            !raw->IsUndefined())
          out[i] = NumOr(context, raw, i == 3 ? 1.0 : 0.0);
      }
    }

    void PointMatrixTransform(
        const v8::FunctionCallbackInfo<v8::Value>& info)
    {
      v8::Isolate* isolate = info.GetIsolate();
      v8::Local<v8::Context> context = isolate->GetCurrentContext();
      v8::Local<v8::Object> self = info.This();
      auto get = [&](const char* name) {
        int idx = PointFieldIndex(name);
        if (self->InternalFieldCount() > idx)
          return NumOr(context, v8::Local<v8::Value>::Cast(self->GetInternalField(idx)), 0.0);
        return 0.0;
      };
      double px = get("_x"), py = get("_y"), pz = get("_z"), pw = get("_w");
      double m[6] = {1, 0, 0, 1, 0, 0};
      if (info.Length() >= 1 && info[0]->IsObject())
      {
        const char* names[] = {"a", "b", "c", "d", "e", "f"};
        for (int i = 0; i < 6; i++)
          m[i] = NumOr(context,
                       info[0].As<v8::Object>()
                           ->Get(context, ToV8Str(isolate, names[i]))
                           .FromMaybe(v8::Local<v8::Value>()),
                       0.0);
      }
      info.GetReturnValue().Set(MakePoint(
          isolate, context, true,
          m[0] * px + m[2] * py + m[4], m[1] * px + m[3] * py + m[5],
          pz, pw));
    }

    void PointToJSON(const v8::FunctionCallbackInfo<v8::Value>& info)
    {
      v8::Isolate* isolate = info.GetIsolate();
      v8::Local<v8::Context> context = isolate->GetCurrentContext();
      v8::Local<v8::Object> self = info.This();
      auto get = [&](const char* name) {
        int idx = PointFieldIndex(name);
        if (self->InternalFieldCount() > idx)
          return NumOr(context, v8::Local<v8::Value>::Cast(self->GetInternalField(idx)), 0.0);
        return 0.0;
      };
      v8::Local<v8::Object> out = v8::Object::New(isolate);
      const char* names[] = {"x", "y", "z", "w"};
      const double values[] = {get("_x"), get("_y"), get("_z"), get("_w")};
      SetNumberProps(isolate, context, out, names, values, 4);
      info.GetReturnValue().Set(out);
    }

    void PointRoFromPoint(const v8::FunctionCallbackInfo<v8::Value>& info)
    {
      v8::Isolate* isolate = info.GetIsolate();
      v8::Local<v8::Context> context = isolate->GetCurrentContext();
      double v[4];
      ReadPointLike(isolate, context,
                    info.Length() >= 1
                        ? v8::Local<v8::Value>(info[0])
                        : v8::Local<v8::Value>(v8::Undefined(isolate)),
                    v);
      info.GetReturnValue().Set(
          MakePoint(isolate, context, false, v[0], v[1], v[2], v[3]));
    }

    void PointFromPoint(const v8::FunctionCallbackInfo<v8::Value>& info)
    {
      v8::Isolate* isolate = info.GetIsolate();
      v8::Local<v8::Context> context = isolate->GetCurrentContext();
      double v[4];
      ReadPointLike(isolate, context,
                    info.Length() >= 1
                        ? v8::Local<v8::Value>(info[0])
                        : v8::Local<v8::Value>(v8::Undefined(isolate)),
                    v);
      info.GetReturnValue().Set(
          MakePoint(isolate, context, true, v[0], v[1], v[2], v[3]));
    }

    // ---- DOMMatrix / DOMMatrixReadOnly -------------------------------

    typedef std::array<double, 6> Matrix6;

    Matrix6 MatrixIdentity()
    {
      Matrix6 m;
      m = {1, 0, 0, 1, 0, 0};
      return m;
    }

    Matrix6 MatrixMul(const Matrix6& a, const Matrix6& b)
    {
      Matrix6 r;
      r[0] = a[0] * b[0] + a[2] * b[1];
      r[1] = a[1] * b[0] + a[3] * b[1];
      r[2] = a[0] * b[2] + a[2] * b[3];
      r[3] = a[1] * b[2] + a[3] * b[3];
      r[4] = a[0] * b[4] + a[2] * b[5] + a[4];
      r[5] = a[1] * b[4] + a[3] * b[5] + a[5];
      return r;
    }

    void SetMatrixProps(v8::Isolate* isolate, v8::Local<v8::Context> context,
                        v8::Local<v8::Object> inst, const Matrix6& m)
    {
      const char* names[] = {"a", "b", "c", "d", "e", "f",
          "m11", "m12", "m13", "m14", "m21", "m22", "m23", "m24",
          "m31", "m32", "m33", "m34", "m41", "m42", "m43", "m44",
          "is2D", "isIdentity"};
      double values[24];
      values[0] = m[0]; values[1] = m[1]; values[2] = m[2];
      values[3] = m[3]; values[4] = m[4]; values[5] = m[5];
      values[6] = m[0]; values[7] = m[1]; values[8] = 0; values[9] = 0;
      values[10] = m[2]; values[11] = m[3]; values[12] = 0; values[13] = 0;
      values[14] = 0; values[15] = 0; values[16] = 1; values[17] = 0;
      values[18] = m[4]; values[19] = m[5]; values[20] = 0; values[21] = 1;
      values[22] = 1;
      values[23] = m[0] == 1 && m[1] == 0 && m[2] == 0 && m[3] == 1 &&
                   m[4] == 0 && m[5] == 0 ? 1 : 0;
      SetNumberProps(isolate, context, inst, names, values, 24);
    }

    v8::Local<v8::Object> MakeMatrix(v8::Isolate* isolate,
                                     v8::Local<v8::Context> context,
                                     const Matrix6& m)
    {
      ContextState* state = GetState(context);
      v8::Local<v8::Object> inst = state->dommatrix_tpl.Get(isolate)
                                       ->InstanceTemplate()
                                       ->NewInstance(context)
                                       .ToLocalChecked();
      SetMatrixProps(isolate, context, inst, m);
      return inst;
    }

    void MatrixInitFromArg(v8::Isolate* isolate,
                           v8::Local<v8::Context> context,
                           v8::Local<v8::Value> init, Matrix6& m)
    {
      m = MatrixIdentity();
      if (init.IsEmpty() || init->IsNullOrUndefined()) return;
      double six[6] = {1, 0, 0, 1, 0, 0};
      bool have = false;
      if (init->IsArray())
      {
        v8::Local<v8::Array> items = init.As<v8::Array>();
        if (items->Length() == 6)
        {
          for (uint32_t i = 0; i < 6; i++)
            six[i] = NumOr(context,
                           items->Get(context, i)
                               .FromMaybe(v8::Local<v8::Value>()),
                           0.0);
          have = true;
        }
      }
      else if (init->IsArrayBufferView())
      {
        v8::Local<v8::ArrayBufferView> view = init.As<v8::ArrayBufferView>();
        if (view->ByteLength() == 24 || view->ByteLength() == 48)
        {
          v8::Local<v8::ArrayBuffer> buffer = view->Buffer();
          size_t total = 0;
          const uint8_t* base = ArrayBufferBytes(buffer, &total);
          size_t start = static_cast<size_t>(view->ByteOffset());
          if (base != nullptr && start + view->ByteLength() <= total)
          {
            for (int i = 0; i < 6; i++)
            {
              if (view->ByteLength() == 24)
              {
                float f;
                std::memcpy(&f, base + start + i * 4, 4);
                six[i] = static_cast<double>(f);
              }
              else
              {
                double d;
                std::memcpy(&d, base + start + i * 8, 8);
                six[i] = d;
              }
            }
            have = true;
          }
        }
      }
      else if (init->IsObject())
      {
        const char* names[] = {"a", "b", "c", "d", "e", "f"};
        for (int i = 0; i < 6; i++)
          six[i] = NumOr(context,
                         init.As<v8::Object>()
                             ->Get(context, ToV8Str(isolate, names[i]))
                             .FromMaybe(v8::Local<v8::Value>()),
                         0.0);
        have = true;
      }
      if (have)
      {
        for (int i = 0; i < 6; i++) m[i] = six[i];
      }
    }

    void MatrixReadOnlyConstructor(
        const v8::FunctionCallbackInfo<v8::Value>& info)
    {
      v8::Isolate* isolate = info.GetIsolate();
      v8::Local<v8::Context> context = isolate->GetCurrentContext();
      if (!info.IsConstructCall())
      {
        isolate->ThrowException(ToV8Str(
            isolate,
            "TypeError: DOMMatrixReadOnly constructor requires new"));
        return;
      }
      Matrix6 m;
      MatrixInitFromArg(isolate, context,
                        info.Length() >= 1 ? info[0]
                                           : v8::Local<v8::Value>(),
                        m);
      // M11: instances must read [object DOMMatrixReadOnly] - MakeMatrix
      // builds on the mutable DOMMatrix template, so the RO constructor
      // forwarded there and the instance tag came out "DOMMatrix"
      ContextState* state = GetState(context);
      if (state != nullptr && !state->dommatrixro_tpl.IsEmpty())
      {
        v8::Local<v8::Object> inst = state->dommatrixro_tpl.Get(isolate)
                                         ->InstanceTemplate()
                                         ->NewInstance(context)
                                         .ToLocalChecked();
        SetMatrixProps(isolate, context, inst, m);
        info.GetReturnValue().Set(inst);
        return;
      }
      info.GetReturnValue().Set(MakeMatrix(isolate, context, m));
    }

    Matrix6 MatrixSelf(const v8::FunctionCallbackInfo<v8::Value>& info)
    {
      v8::Isolate* isolate = info.GetIsolate();
      v8::Local<v8::Context> context = isolate->GetCurrentContext();
      auto get = [&](const char* name) {
        return NumOr(context,
                     info.This()->Get(context, ToV8Str(isolate, name))
                         .FromMaybe(v8::Local<v8::Value>()),
                     0.0);
      };
      Matrix6 self = {get("a"), get("b"), get("c"),
                      get("d"), get("e"), get("f")};
      return self;
    }

    void MatrixMultiply(const v8::FunctionCallbackInfo<v8::Value>& info)
    {
      v8::Isolate* isolate = info.GetIsolate();
      v8::Local<v8::Context> context = isolate->GetCurrentContext();
      Matrix6 self = MatrixSelf(info);
      Matrix6 other = MatrixIdentity();
      MatrixInitFromArg(isolate, context,
                        info.Length() >= 1 ? info[0]
                                           : v8::Local<v8::Value>(),
                        other);
      info.GetReturnValue().Set(
          MakeMatrix(isolate, context, MatrixMul(self, other)));
    }

    void MatrixTranslate(const v8::FunctionCallbackInfo<v8::Value>& info)
    {
      v8::Isolate* isolate = info.GetIsolate();
      v8::Local<v8::Context> context = isolate->GetCurrentContext();
      Matrix6 self = MatrixSelf(info);
      double tx = info.Length() >= 1 ? NumOr(context, info[0], 0.0) : 0.0;
      double ty = info.Length() >= 2 ? NumOr(context, info[1], 0.0) : 0.0;
      Matrix6 t = {1, 0, 0, 1, tx, ty};
      info.GetReturnValue().Set(
          MakeMatrix(isolate, context, MatrixMul(self, t)));
    }

    void MatrixScale(const v8::FunctionCallbackInfo<v8::Value>& info)
    {
      v8::Isolate* isolate = info.GetIsolate();
      v8::Local<v8::Context> context = isolate->GetCurrentContext();
      Matrix6 self = MatrixSelf(info);
      double scale = info.Length() >= 1 ? NumOr(context, info[0], 1.0) : 1.0;
      double ox = info.Length() >= 2 ? NumOr(context, info[1], 0.0) : 0.0;
      double oy = info.Length() >= 3 ? NumOr(context, info[2], 0.0) : 0.0;
      Matrix6 t1 = {1, 0, 0, 1, ox, oy};
      Matrix6 s = {scale, 0, 0, scale, 0, 0};
      Matrix6 t2 = {1, 0, 0, 1, -ox, -oy};
      info.GetReturnValue().Set(MakeMatrix(
          isolate, context, MatrixMul(MatrixMul(MatrixMul(self, t1), s), t2)));
    }

    void MatrixScaleNonUniform(
        const v8::FunctionCallbackInfo<v8::Value>& info)
    {
      v8::Isolate* isolate = info.GetIsolate();
      v8::Local<v8::Context> context = isolate->GetCurrentContext();
      Matrix6 self = MatrixSelf(info);
      double sx = info.Length() >= 1 ? NumOr(context, info[0], 1.0) : 1.0;
      double sy = info.Length() >= 2 ? NumOr(context, info[1], 1.0) : 1.0;
      double ox = info.Length() >= 4 ? NumOr(context, info[3], 0.0) : 0.0;
      double oy = info.Length() >= 5 ? NumOr(context, info[4], 0.0) : 0.0;
      Matrix6 t1 = {1, 0, 0, 1, ox, oy};
      Matrix6 s = {sx, 0, 0, sy, 0, 0};
      Matrix6 t2 = {1, 0, 0, 1, -ox, -oy};
      info.GetReturnValue().Set(MakeMatrix(
          isolate, context, MatrixMul(MatrixMul(MatrixMul(self, t1), s), t2)));
    }

    void MatrixRotate(const v8::FunctionCallbackInfo<v8::Value>& info)
    {
      v8::Isolate* isolate = info.GetIsolate();
      v8::Local<v8::Context> context = isolate->GetCurrentContext();
      Matrix6 self = MatrixSelf(info);
      double angle = info.Length() >= 1 ? NumOr(context, info[0], 0.0) : 0.0;
      double radians = angle * 3.14159265358979323846 / 180.0;
      double c = std::cos(radians), s = std::sin(radians);
      Matrix6 r = {c, s, -s, c, 0, 0};
      info.GetReturnValue().Set(
          MakeMatrix(isolate, context, MatrixMul(self, r)));
    }

    void MatrixInverse(const v8::FunctionCallbackInfo<v8::Value>& info)
    {
      v8::Isolate* isolate = info.GetIsolate();
      v8::Local<v8::Context> context = isolate->GetCurrentContext();
      Matrix6 self = MatrixSelf(info);
      double det = self[0] * self[3] - self[1] * self[2];
      Matrix6 r;
      if (det == 0)
      {
        double nan = std::numeric_limits<double>::quiet_NaN();
        r = {nan, nan, nan, nan, nan, nan};
      }
      else
      {
        r = {self[3] / det, -self[1] / det, -self[2] / det, self[0] / det,
             (self[2] * self[5] - self[3] * self[4]) / det,
             (self[1] * self[4] - self[0] * self[5]) / det};
      }
      info.GetReturnValue().Set(MakeMatrix(isolate, context, r));
    }

    void MatrixTransformPoint(
        const v8::FunctionCallbackInfo<v8::Value>& info)
    {
      v8::Isolate* isolate = info.GetIsolate();
      v8::Local<v8::Context> context = isolate->GetCurrentContext();
      Matrix6 self = MatrixSelf(info);
      double p[4];
      ReadPointLike(isolate, context,
                    info.Length() >= 1
                        ? v8::Local<v8::Value>(info[0])
                        : v8::Local<v8::Value>(v8::Undefined(isolate)),
                    p);
      info.GetReturnValue().Set(MakePoint(
          isolate, context, true,
          self[0] * p[0] + self[2] * p[1] + self[4],
          self[1] * p[0] + self[3] * p[1] + self[5], p[2], p[3]));
    }

    void MatrixToFloat32Array(
        const v8::FunctionCallbackInfo<v8::Value>& info)
    {
      v8::Isolate* isolate = info.GetIsolate();
      v8::Local<v8::Context> context = isolate->GetCurrentContext();
      Matrix6 self = MatrixSelf(info);
      const float values[16] = {
          static_cast<float>(self[0]), static_cast<float>(self[1]), 0, 0,
          static_cast<float>(self[2]), static_cast<float>(self[3]), 0, 0,
          0, 0, 1, 0,
          static_cast<float>(self[4]), static_cast<float>(self[5]), 0, 1};
      v8::Local<v8::ArrayBuffer> buffer =
          v8::ArrayBuffer::New(isolate, 16 * sizeof(float));
      std::memcpy(buffer->GetBackingStore()->Data(), values,
                  16 * sizeof(float));
      info.GetReturnValue().Set(v8::Float32Array::New(buffer, 0, 16));
    }

    void MatrixToFloat64Array(
        const v8::FunctionCallbackInfo<v8::Value>& info)
    {
      v8::Isolate* isolate = info.GetIsolate();
      v8::Local<v8::Context> context = isolate->GetCurrentContext();
      Matrix6 self = MatrixSelf(info);
      const double values[16] = {
          self[0], self[1], 0, 0,
          self[2], self[3], 0, 0,
          0, 0, 1, 0,
          self[4], self[5], 0, 1};
      v8::Local<v8::ArrayBuffer> buffer =
          v8::ArrayBuffer::New(isolate, 16 * sizeof(double));
      std::memcpy(buffer->GetBackingStore()->Data(), values,
                  16 * sizeof(double));
      info.GetReturnValue().Set(v8::Float64Array::New(buffer, 0, 16));
    }

    void MatrixToString(const v8::FunctionCallbackInfo<v8::Value>& info)
    {
      v8::Isolate* isolate = info.GetIsolate();
      v8::Local<v8::Context> context = isolate->GetCurrentContext();
      Matrix6 self = MatrixSelf(info);
      std::ostringstream out;
      out << "matrix(" << self[0] << ", " << self[1] << ", " << self[2]
          << ", " << self[3] << ", " << self[4] << ", " << self[5] << ")";
      info.GetReturnValue().Set(ToV8Str(isolate, out.str()));
    }

    void MatrixRoFromMatrix(const v8::FunctionCallbackInfo<v8::Value>& info)
    {
      v8::Isolate* isolate = info.GetIsolate();
      v8::Local<v8::Context> context = isolate->GetCurrentContext();
      Matrix6 m;
      MatrixInitFromArg(isolate, context,
                        info.Length() >= 1 ? info[0]
                                           : v8::Local<v8::Value>(),
                        m);
      info.GetReturnValue().Set(MakeMatrix(isolate, context, m));
    }

    void MatrixConstructor(const v8::FunctionCallbackInfo<v8::Value>& info)
    {
      v8::Isolate* isolate = info.GetIsolate();
      v8::Local<v8::Context> context = isolate->GetCurrentContext();
      if (!info.IsConstructCall())
      {
        isolate->ThrowException(ToV8Str(
            isolate, "TypeError: DOMMatrix constructor requires new"));
        return;
      }
      if (info.Length() >= 1 && info[0]->IsString())
      {
        // DOMMatrix("matrix(a, b, c, d, e, f)") string form
        std::string text = ToUtf8(isolate, info[0]);
        std::string lower;
        for (char ch : text)
          lower += static_cast<char>(
              std::tolower(static_cast<unsigned char>(ch)));
        size_t open = lower.find("matrix(");
        bool ok = open != std::string::npos &&
                  lower[lower.size() - 1] == ')';
        double six[6] = {1, 0, 0, 1, 0, 0};
        if (ok)
        {
          std::string inner = text.substr(open + 7, text.size() - open - 8);
          int count = 0;
          size_t pos = 0;
          while (count < 6)
          {
            size_t comma = inner.find(',', pos);
            std::string token = comma == std::string::npos
                ? inner.substr(pos)
                : inner.substr(pos, comma - pos);
            try
            {
              six[count] = std::stod(token);
            }
            catch (...)
            {
              ok = false;
              break;
            }
            count += 1;
            if (comma == std::string::npos) break;
            pos = comma + 1;
          }
          if (count != 6) ok = false;
        }
        if (!ok)
        {
          isolate->ThrowException(
              ToV8Str(isolate, "SyntaxError: Invalid matrix"));
          return;
        }
        Matrix6 m = {six[0], six[1], six[2], six[3], six[4], six[5]};
        info.GetReturnValue().Set(MakeMatrix(isolate, context, m));
        return;
      }
      Matrix6 m;
      MatrixInitFromArg(isolate, context,
                        info.Length() >= 1 ? info[0]
                                           : v8::Local<v8::Value>(),
                        m);
      info.GetReturnValue().Set(MakeMatrix(isolate, context, m));
    }

    // *Self mutators: recompute then copy the 24 own props back
    // (JS Object.assign semantics) and return this
    void MatrixMultiplySelf(const v8::FunctionCallbackInfo<v8::Value>& info)
    {
      v8::Isolate* isolate = info.GetIsolate();
      v8::Local<v8::Context> context = isolate->GetCurrentContext();
      Matrix6 self = MatrixSelf(info);
      Matrix6 other = MatrixIdentity();
      MatrixInitFromArg(isolate, context,
                        info.Length() >= 1 ? info[0]
                                           : v8::Local<v8::Value>(),
                        other);
      Matrix6 r = MatrixMul(self, other);
      SetMatrixProps(isolate, context, info.This(), r);
      info.GetReturnValue().Set(info.This());
    }

    void MatrixPreMultiplySelf(
        const v8::FunctionCallbackInfo<v8::Value>& info)
    {
      v8::Isolate* isolate = info.GetIsolate();
      v8::Local<v8::Context> context = isolate->GetCurrentContext();
      Matrix6 self = MatrixSelf(info);
      Matrix6 other = MatrixIdentity();
      MatrixInitFromArg(isolate, context,
                        info.Length() >= 1 ? info[0]
                                           : v8::Local<v8::Value>(),
                        other);
      Matrix6 r = MatrixMul(other, self);
      SetMatrixProps(isolate, context, info.This(), r);
      info.GetReturnValue().Set(info.This());
    }

    void MatrixTranslateSelf(const v8::FunctionCallbackInfo<v8::Value>& info)
    {
      v8::Isolate* isolate = info.GetIsolate();
      v8::Local<v8::Context> context = isolate->GetCurrentContext();
      Matrix6 self = MatrixSelf(info);
      double tx = info.Length() >= 1 ? NumOr(context, info[0], 0.0) : 0.0;
      double ty = info.Length() >= 2 ? NumOr(context, info[1], 0.0) : 0.0;
      Matrix6 t = {1, 0, 0, 1, tx, ty};
      SetMatrixProps(isolate, context, info.This(), MatrixMul(self, t));
      info.GetReturnValue().Set(info.This());
    }

    void MatrixScaleSelf(const v8::FunctionCallbackInfo<v8::Value>& info)
    {
      v8::Isolate* isolate = info.GetIsolate();
      v8::Local<v8::Context> context = isolate->GetCurrentContext();
      Matrix6 self = MatrixSelf(info);
      double scale = info.Length() >= 1 ? NumOr(context, info[0], 1.0) : 1.0;
      double ox = info.Length() >= 2 ? NumOr(context, info[1], 0.0) : 0.0;
      double oy = info.Length() >= 3 ? NumOr(context, info[2], 0.0) : 0.0;
      Matrix6 t1 = {1, 0, 0, 1, ox, oy};
      Matrix6 s = {scale, 0, 0, scale, 0, 0};
      Matrix6 t2 = {1, 0, 0, 1, -ox, -oy};
      SetMatrixProps(isolate, context, info.This(),
                     MatrixMul(MatrixMul(MatrixMul(self, t1), s), t2));
      info.GetReturnValue().Set(info.This());
    }

    void MatrixScaleNonUniformSelf(
        const v8::FunctionCallbackInfo<v8::Value>& info)
    {
      v8::Isolate* isolate = info.GetIsolate();
      v8::Local<v8::Context> context = isolate->GetCurrentContext();
      Matrix6 self = MatrixSelf(info);
      double sx = info.Length() >= 1 ? NumOr(context, info[0], 1.0) : 1.0;
      double sy = info.Length() >= 2 ? NumOr(context, info[1], 1.0) : 1.0;
      double ox = info.Length() >= 4 ? NumOr(context, info[3], 0.0) : 0.0;
      double oy = info.Length() >= 5 ? NumOr(context, info[4], 0.0) : 0.0;
      Matrix6 t1 = {1, 0, 0, 1, ox, oy};
      Matrix6 s = {sx, 0, 0, sy, 0, 0};
      Matrix6 t2 = {1, 0, 0, 1, -ox, -oy};
      SetMatrixProps(isolate, context, info.This(),
                     MatrixMul(MatrixMul(MatrixMul(self, t1), s), t2));
      info.GetReturnValue().Set(info.This());
    }

    void MatrixRotateSelf(const v8::FunctionCallbackInfo<v8::Value>& info)
    {
      v8::Isolate* isolate = info.GetIsolate();
      v8::Local<v8::Context> context = isolate->GetCurrentContext();
      Matrix6 self = MatrixSelf(info);
      double angle = info.Length() >= 1 ? NumOr(context, info[0], 0.0) : 0.0;
      double radians = angle * 3.14159265358979323846 / 180.0;
      double c = std::cos(radians), s = std::sin(radians);
      Matrix6 r = {c, s, -s, c, 0, 0};
      SetMatrixProps(isolate, context, info.This(), MatrixMul(self, r));
      info.GetReturnValue().Set(info.This());
    }

    void MatrixInvertSelf(const v8::FunctionCallbackInfo<v8::Value>& info)
    {
      v8::Isolate* isolate = info.GetIsolate();
      v8::Local<v8::Context> context = isolate->GetCurrentContext();
      Matrix6 self = MatrixSelf(info);
      double det = self[0] * self[3] - self[1] * self[2];
      Matrix6 r;
      if (det == 0)
      {
        double nan = std::numeric_limits<double>::quiet_NaN();
        r = {nan, nan, nan, nan, nan, nan};
      }
      else
      {
        r = {self[3] / det, -self[1] / det, -self[2] / det, self[0] / det,
             (self[2] * self[5] - self[3] * self[4]) / det,
             (self[1] * self[4] - self[0] * self[5]) / det};
      }
      SetMatrixProps(isolate, context, info.This(), r);
      info.GetReturnValue().Set(info.This());
    }

    void MatrixSetMatrixValue(
        const v8::FunctionCallbackInfo<v8::Value>& info)
    {
      v8::Isolate* isolate = info.GetIsolate();
      v8::Local<v8::Context> context = isolate->GetCurrentContext();
      if (info.Length() < 1) return;
      std::string text = ToUtf8(isolate, info[0]);
      std::string lower;
      for (char ch : text)
        lower += static_cast<char>(
            std::tolower(static_cast<unsigned char>(ch)));
      size_t open = lower.find("matrix(");
      double six[6] = {1, 0, 0, 1, 0, 0};
      bool ok = open != std::string::npos &&
                lower[lower.size() - 1] == ')';
      if (ok)
      {
        std::string inner = text.substr(open + 7, text.size() - open - 8);
        int count = 0;
        size_t pos = 0;
        while (count < 6)
        {
          size_t comma = inner.find(',', pos);
          std::string token = comma == std::string::npos
              ? inner.substr(pos)
              : inner.substr(pos, comma - pos);
          try
          {
            six[count] = std::stod(token);
          }
          catch (...)
          {
            ok = false;
            break;
          }
          count += 1;
          if (comma == std::string::npos) break;
          pos = comma + 1;
        }
        if (count != 6) ok = false;
      }
      if (!ok)
      {
        isolate->ThrowException(
            ToV8Str(isolate, "SyntaxError: Invalid matrix"));
        return;
      }
      Matrix6 m = {six[0], six[1], six[2], six[3], six[4], six[5]};
      SetMatrixProps(isolate, context, info.This(), m);
      info.GetReturnValue().Set(info.This());
    }

    void MatrixFromMatrix(const v8::FunctionCallbackInfo<v8::Value>& info)
    {
      v8::Isolate* isolate = info.GetIsolate();
      v8::Local<v8::Context> context = isolate->GetCurrentContext();
      Matrix6 m;
      MatrixInitFromArg(isolate, context,
                        info.Length() >= 1 ? info[0]
                                           : v8::Local<v8::Value>(),
                        m);
      info.GetReturnValue().Set(MakeMatrix(isolate, context, m));
    }

    // ---- DOMQuad -----------------------------------------------------

    void QuadConstructor(const v8::FunctionCallbackInfo<v8::Value>& info)
    {
      v8::Isolate* isolate = info.GetIsolate();
      v8::Local<v8::Context> context = isolate->GetCurrentContext();
      ContextState* state = GetState(context);
      if (!info.IsConstructCall() || state == nullptr ||
          state->domquad_tpl.IsEmpty())
      {
        isolate->ThrowException(
            ToV8Str(isolate, "TypeError: DOMQuad constructor requires new"));
        return;
      }
      v8::Local<v8::Object> inst = state->domquad_tpl.Get(isolate)
                                       ->InstanceTemplate()
                                       ->NewInstance(context)
                                       .ToLocalChecked();
      const char* names[] = {"p1", "p2", "p3", "p4"};
      for (int i = 0; i < 4; i++)
      {
        double p[4];
        ReadPointLike(isolate, context,
                      info.Length() > i
                          ? v8::Local<v8::Value>(info[i])
                          : v8::Local<v8::Value>(v8::Undefined(isolate)),
                      p);
        inst->SetInternalField(i, MakePoint(isolate, context, false,
                                            p[0], p[1], p[2], p[3]));
      }
      info.GetReturnValue().Set(inst);
    }

    // M12: p1..p4 live in internal fields 0..3 (reference quad instances are
    // zero-own); the prototype accessors surface them
    void QuadPointComponent(
        const v8::FunctionCallbackInfo<v8::Value>& info, int idx)
    {
      v8::Local<v8::Object> self = info.This();
      if (self->InternalFieldCount() > idx)
      {
        info.GetReturnValue().Set(v8::Local<v8::Value>::Cast(self->GetInternalField(idx)));
        return;
      }
      info.GetReturnValue().SetNull();
    }
    void QuadGetP1(const v8::FunctionCallbackInfo<v8::Value>& info)
    { QuadPointComponent(info, 0); }
    void QuadGetP2(const v8::FunctionCallbackInfo<v8::Value>& info)
    { QuadPointComponent(info, 1); }
    void QuadGetP3(const v8::FunctionCallbackInfo<v8::Value>& info)
    { QuadPointComponent(info, 2); }
    void QuadGetP4(const v8::FunctionCallbackInfo<v8::Value>& info)
    { QuadPointComponent(info, 3); }

    void QuadGetBounds(const v8::FunctionCallbackInfo<v8::Value>& info)
    {
      v8::Isolate* isolate = info.GetIsolate();
      v8::Local<v8::Context> context = isolate->GetCurrentContext();
      double xs[4], ys[4];
      const char* names[] = {"p1", "p2", "p3", "p4"};
      for (int i = 0; i < 4; i++)
      {
        v8::Local<v8::Object> self = info.This();
        v8::Local<v8::Value> p =
            self->InternalFieldCount() > i
                ? v8::Local<v8::Value>::Cast(self->GetInternalField(i))
                : v8::Local<v8::Value>();
        double point[4];
        ReadPointLike(isolate, context, p, point);
        xs[i] = point[0];
        ys[i] = point[1];
      }
      double minX = xs[0], maxX = xs[0], minY = ys[0], maxY = ys[0];
      for (int i = 1; i < 4; i++)
      {
        minX = std::min(minX, xs[i]); maxX = std::max(maxX, xs[i]);
        minY = std::min(minY, ys[i]); maxY = std::max(maxY, ys[i]);
      }
      info.GetReturnValue().Set(MakeRect(isolate, context, minX, minY,
                                         maxX - minX, maxY - minY));
    }

    void QuadFromRect(const v8::FunctionCallbackInfo<v8::Value>& info)
    {
      v8::Isolate* isolate = info.GetIsolate();
      v8::Local<v8::Context> context = isolate->GetCurrentContext();
      ContextState* state = GetState(context);
      double x = 0, y = 0, w = 0, h = 0;
      if (info.Length() >= 1 && info[0]->IsObject())
      {
        const char* names[] = {"x", "y", "width", "height"};
        double* out[] = {&x, &y, &w, &h};
        for (int i = 0; i < 4; i++)
          *out[i] = NumOr(context,
                          info[0].As<v8::Object>()
                              ->Get(context, ToV8Str(isolate, names[i]))
                              .FromMaybe(v8::Local<v8::Value>()),
                          0.0);
      }
      if (state == nullptr || state->domquad_tpl.IsEmpty())
      {
        isolate->ThrowException(
            ToV8Str(isolate, "TypeError: DOMQuad unavailable"));
        return;
      }
      v8::Local<v8::Object> inst = state->domquad_tpl.Get(isolate)
                                       ->InstanceTemplate()
                                       ->NewInstance(context)
                                       .ToLocalChecked();
      const char* point_names[] = {"p1", "p2", "p3", "p4"};
      double coords[4][4] = {
          {x, y, 0, 1}, {x + w, y, 0, 1}, {x + w, y + h, 0, 1},
          {x, y + h, 0, 1}};
      for (int i = 0; i < 4; i++)
        inst->SetInternalField(i, MakePoint(isolate, context, false,
                                            coords[i][0], coords[i][1],
                                            coords[i][2], coords[i][3]));
      info.GetReturnValue().Set(inst);
    }

    void QuadFromQuad(const v8::FunctionCallbackInfo<v8::Value>& info)
    {
      v8::Isolate* isolate = info.GetIsolate();
      v8::Local<v8::Context> context = isolate->GetCurrentContext();
      ContextState* state = GetState(context);
      if (state == nullptr || state->domquad_tpl.IsEmpty())
      {
        isolate->ThrowException(
            ToV8Str(isolate, "TypeError: DOMQuad unavailable"));
        return;
      }
      v8::Local<v8::Object> inst = state->domquad_tpl.Get(isolate)
                                       ->InstanceTemplate()
                                       ->NewInstance(context)
                                       .ToLocalChecked();
      const char* names[] = {"p1", "p2", "p3", "p4"};
      v8::Local<v8::Value> source =
          info.Length() >= 1 && info[0]->IsObject()
              ? info[0]
              : v8::Local<v8::Value>();
      for (int i = 0; i < 4; i++)
      {
        double p[4] = {0, 0, 0, 1};
        if (!source.IsEmpty())
        {
          v8::Local<v8::Value> raw;
          if (source.As<v8::Object>()
                  ->Get(context, ToV8Str(isolate, names[i]))
                  .ToLocal(&raw))
            ReadPointLike(isolate, context, raw, p);
        }
        inst->SetInternalField(i, MakePoint(isolate, context, false,
                                            p[0], p[1], p[2], p[3]));
      }
      info.GetReturnValue().Set(inst);
    }

    // ---- DataTransfer family ------------------------------------------

    v8::Local<v8::Object> MakeDataTransferItem(
        v8::Isolate* isolate, v8::Local<v8::Context> context,
        const char* kind, v8::Local<v8::Value> type,
        v8::Local<v8::Value> value)
    {
      ContextState* state = GetState(context);
      v8::Local<v8::Object> inst = state->dt_item_tpl.Get(isolate)
                                       ->InstanceTemplate()
                                       ->NewInstance(context)
                                       .ToLocalChecked();
      (void)inst->Set(context, ToV8Str(isolate, "kind"),
                      ToV8Str(isolate, kind))
          .FromMaybe(false);
      (void)inst->Set(context, ToV8Str(isolate, "type"), type)
          .FromMaybe(false);
      (void)inst->SetPrivate(context, DtiValueKey(isolate), value)
          .FromMaybe(false);
      return inst;
    }

    void DataTransferItemConstructor(
        const v8::FunctionCallbackInfo<v8::Value>& info)
    {
      v8::Isolate* isolate = info.GetIsolate();
      v8::Local<v8::Context> context = isolate->GetCurrentContext();
      ContextState* state = GetState(context);
      if (!info.IsConstructCall() || state == nullptr ||
          state->dt_item_tpl.IsEmpty())
      {
        isolate->ThrowException(ToV8Str(
            isolate,
            "TypeError: DataTransferItem constructor requires new"));
        return;
      }
      v8::Local<v8::Value> undefined = v8::Undefined(isolate);
      info.GetReturnValue().Set(MakeDataTransferItem(
          isolate, context,
          info.Length() >= 1 ? ToUtf8(isolate, info[0]).c_str() : "",
          info.Length() >= 2 ? info[1] : undefined,
          info.Length() >= 3 ? info[2] : undefined));
    }

    void DataTransferItemGetAsFile(
        const v8::FunctionCallbackInfo<v8::Value>& info)
    {
      v8::Isolate* isolate = info.GetIsolate();
      v8::Local<v8::Context> context = isolate->GetCurrentContext();
      v8::Local<v8::Value> kind;
      if (info.This()->Get(context, ToV8Str(isolate, "kind")).ToLocal(&kind) &&
          kind->IsString() &&
          ToUtf8(isolate, kind) == "file")
      {
        v8::Local<v8::Value> value;
        if (info.This()
                ->GetPrivate(context, DtiValueKey(isolate))
                .ToLocal(&value) &&
            !value.IsEmpty())
          info.GetReturnValue().Set(value);
        return;
      }
      info.GetReturnValue().SetNull();
    }

    void DataTransferItemGetAsString(
        const v8::FunctionCallbackInfo<v8::Value>& info)
    {
      v8::Isolate* isolate = info.GetIsolate();
      v8::Local<v8::Context> context = isolate->GetCurrentContext();
      v8::Local<v8::Value> kind;
      if (!info.This()->Get(context, ToV8Str(isolate, "kind"))
               .ToLocal(&kind) ||
          !kind->IsString() || ToUtf8(isolate, kind) != "string")
        return;
      if (info.Length() < 1 || !info[0]->IsFunction()) return;
      v8::Local<v8::Array> payload = v8::Array::New(isolate, 2);
      (void)payload->Set(context, 0, info[0]).FromMaybe(false);
      v8::Local<v8::Value> value;
      if (info.This()->GetPrivate(context, DtiValueKey(isolate))
              .ToLocal(&value))
        (void)payload->Set(context, 1, value).FromMaybe(false);
      v8::Local<v8::Value> queue;
      if (!context->Global()
               ->Get(context, ToV8Str(isolate, "queueMicrotask"))
               .ToLocal(&queue) ||
          !queue->IsFunction())
        return;
      v8::Local<v8::FunctionTemplate> runner =
          v8::FunctionTemplate::New(
              isolate,
              [](const v8::FunctionCallbackInfo<v8::Value>& args) {
                v8::Isolate* iso = args.GetIsolate();
                v8::Local<v8::Context> ctx = iso->GetCurrentContext();
                v8::Local<v8::Array> data = args.Data().As<v8::Array>();
                v8::Local<v8::Value> callback, value;
                if (data->Get(ctx, 0).ToLocal(&callback) &&
                    callback->IsFunction() &&
                    data->Get(ctx, 1).ToLocal(&value))
                {
                  v8::Local<v8::Value> argv[] = {value};
                  v8::TryCatch try_catch(iso);
                  (void)callback.As<v8::Function>()
                      ->Call(ctx, ctx->Global(), 1, argv)
                      .FromMaybe(v8::Local<v8::Value>());
                  if (try_catch.HasCaught())
                    CJavascriptException::ThrowIf(iso, try_catch);
                }
              },
              payload);
      runner->SetClassName(ToV8Str(isolate, ""));
      v8::Local<v8::Function> runner_fn =
          runner->GetFunction(context).ToLocalChecked();
      v8::Local<v8::Value> argv[] = {runner_fn};
      (void)queue.As<v8::Function>()
          ->Call(context, context->Global(), 1, argv)
          .FromMaybe(v8::Local<v8::Value>());
    }

    void DataTransferItemWebkitGetAsEntry(
        const v8::FunctionCallbackInfo<v8::Value>& info)
    {
      info.GetReturnValue().SetNull();
    }

    // snapshot of [string items..., file items...] as DataTransferItems
    v8::Local<v8::Array> BuildItemsArray(
        v8::Isolate* isolate, v8::Local<v8::Context> context,
        v8::Local<v8::Object> transfer)
    {
      v8::Local<v8::Array> out = v8::Array::New(isolate);
      uint32_t n = 0;
      v8::Local<v8::Value> data;
      if (transfer->GetPrivate(context, DtDataKey(isolate)).ToLocal(&data) &&
          data->IsMap())
      {
        v8::Local<v8::Array> flat = data.As<v8::Map>()->AsArray();
        for (uint32_t i = 0; i + 1 < flat->Length(); i += 2)
        {
          v8::Local<v8::Value> type, value;
          if (flat->Get(context, i).ToLocal(&type) &&
              flat->Get(context, i + 1).ToLocal(&value))
            (void)out->Set(context, n++,
                           MakeDataTransferItem(isolate, context, "string",
                                                type, value))
                .FromMaybe(false);
        }
      }
      v8::Local<v8::Value> files;
      if (transfer->GetPrivate(context, DtFilesKey(isolate)).ToLocal(&files) &&
          files->IsArray())
      {
        v8::Local<v8::Array> list = files.As<v8::Array>();
        for (uint32_t i = 0; i < list->Length(); i++)
        {
          v8::Local<v8::Value> file;
          if (list->Get(context, i).ToLocal(&file))
          {
            v8::Local<v8::Value> type;
            if (file->IsObject())
              type = file.As<v8::Object>()
                         ->Get(context, ToV8Str(isolate, "type"))
                         .FromMaybe(v8::Local<v8::Value>());
            (void)out->Set(context, n++,
                           MakeDataTransferItem(isolate, context, "file",
                                                type, file))
                .FromMaybe(false);
          }
        }
      }
      return out;
    }

static const char* const kCssComputedCamelNames[] = {
    "accentColor",
    "additiveSymbols",
    "alignContent",
    "alignItems",
    "alignSelf",
    "alignmentBaseline",
    "all",
    "anchorName",
    "animation",
    "animationComposition",
    "animationDelay",
    "animationDirection",
    "animationDuration",
    "animationFillMode",
    "animationIterationCount",
    "animationName",
    "animationPlayState",
    "animationRange",
    "animationRangeEnd",
    "animationRangeStart",
    "animationTimeline",
    "animationTimingFunction",
    "appRegion",
    "appearance",
    "ascentOverride",
    "aspectRatio",
    "backdropFilter",
    "backfaceVisibility",
    "background",
    "backgroundAttachment",
    "backgroundBlendMode",
    "backgroundClip",
    "backgroundColor",
    "backgroundImage",
    "backgroundOrigin",
    "backgroundPosition",
    "backgroundPositionX",
    "backgroundPositionY",
    "backgroundRepeat",
    "backgroundSize",
    "basePalette",
    "baselineShift",
    "baselineSource",
    "blockSize",
    "border",
    "borderBlock",
    "borderBlockColor",
    "borderBlockEnd",
    "borderBlockEndColor",
    "borderBlockEndStyle",
    "borderBlockEndWidth",
    "borderBlockStart",
    "borderBlockStartColor",
    "borderBlockStartStyle",
    "borderBlockStartWidth",
    "borderBlockStyle",
    "borderBlockWidth",
    "borderBottom",
    "borderBottomColor",
    "borderBottomLeftRadius",
    "borderBottomRightRadius",
    "borderBottomStyle",
    "borderBottomWidth",
    "borderCollapse",
    "borderColor",
    "borderEndEndRadius",
    "borderEndStartRadius",
    "borderImage",
    "borderImageOutset",
    "borderImageRepeat",
    "borderImageSlice",
    "borderImageSource",
    "borderImageWidth",
    "borderInline",
    "borderInlineColor",
    "borderInlineEnd",
    "borderInlineEndColor",
    "borderInlineEndStyle",
    "borderInlineEndWidth",
    "borderInlineStart",
    "borderInlineStartColor",
    "borderInlineStartStyle",
    "borderInlineStartWidth",
    "borderInlineStyle",
    "borderInlineWidth",
    "borderLeft",
    "borderLeftColor",
    "borderLeftStyle",
    "borderLeftWidth",
    "borderRadius",
    "borderRight",
    "borderRightColor",
    "borderRightStyle",
    "borderRightWidth",
    "borderSpacing",
    "borderStartEndRadius",
    "borderStartStartRadius",
    "borderStyle",
    "borderTop",
    "borderTopColor",
    "borderTopLeftRadius",
    "borderTopRightRadius",
    "borderTopStyle",
    "borderTopWidth",
    "borderWidth",
    "bottom",
    "boxShadow",
    "boxSizing",
    "breakAfter",
    "breakBefore",
    "breakInside",
    "bufferedRendering",
    "captionSide",
    "caretColor",
    "clear",
    "clip",
    "clipPath",
    "clipRule",
    "color",
    "colorInterpolation",
    "colorInterpolationFilters",
    "colorRendering",
    "colorScheme",
    "columnCount",
    "columnFill",
    "columnGap",
    "columnRule",
    "columnRuleColor",
    "columnRuleStyle",
    "columnRuleWidth",
    "columnSpan",
    "columnWidth",
    "columns",
    "contain",
    "containIntrinsicBlockSize",
    "containIntrinsicHeight",
    "containIntrinsicInlineSize",
    "containIntrinsicSize",
    "containIntrinsicWidth",
    "container",
    "containerName",
    "containerType",
    "content",
    "contentVisibility",
    "counterIncrement",
    "counterReset",
    "counterSet",
    "cursor",
    "cx",
    "cy",
    "d",
    "descentOverride",
    "direction",
    "display",
    "dominantBaseline",
    "emptyCells",
    "fallback",
    "fieldSizing",
    "fill",
    "fillOpacity",
    "fillRule",
    "filter",
    "flex",
    "flexBasis",
    "flexDirection",
    "flexFlow",
    "flexGrow",
    "flexShrink",
    "flexWrap",
    "float",
    "floodColor",
    "floodOpacity",
    "font",
    "fontDisplay",
    "fontFamily",
    "fontFeatureSettings",
    "fontKerning",
    "fontOpticalSizing",
    "fontPalette",
    "fontSize",
    "fontStretch",
    "fontStyle",
    "fontSynthesis",
    "fontSynthesisSmallCaps",
    "fontSynthesisStyle",
    "fontSynthesisWeight",
    "fontVariant",
    "fontVariantAlternates",
    "fontVariantCaps",
    "fontVariantEastAsian",
    "fontVariantLigatures",
    "fontVariantNumeric",
    "fontVariantPosition",
    "fontVariationSettings",
    "fontWeight",
    "forcedColorAdjust",
    "gap",
    "grid",
    "gridArea",
    "gridAutoColumns",
    "gridAutoFlow",
    "gridAutoRows",
    "gridColumn",
    "gridColumnEnd",
    "gridColumnGap",
    "gridColumnStart",
    "gridGap",
    "gridRow",
    "gridRowEnd",
    "gridRowGap",
    "gridRowStart",
    "gridTemplate",
    "gridTemplateAreas",
    "gridTemplateColumns",
    "gridTemplateRows",
    "height",
    "hyphenateCharacter",
    "hyphenateLimitChars",
    "hyphens",
    "imageOrientation",
    "imageRendering",
    "inherits",
    "initialLetter",
    "initialValue",
    "inlineSize",
    "inset",
    "insetArea",
    "insetBlock",
    "insetBlockEnd",
    "insetBlockStart",
    "insetInline",
    "insetInlineEnd",
    "insetInlineStart",
    "isolation",
    "justifyContent",
    "justifyItems",
    "justifySelf",
    "left",
    "letterSpacing",
    "lightingColor",
    "lineBreak",
    "lineGapOverride",
    "lineHeight",
    "listStyle",
    "listStyleImage",
    "listStylePosition",
    "listStyleType",
    "margin",
    "marginBlock",
    "marginBlockEnd",
    "marginBlockStart",
    "marginBottom",
    "marginInline",
    "marginInlineEnd",
    "marginInlineStart",
    "marginLeft",
    "marginRight",
    "marginTop",
    "marker",
    "markerEnd",
    "markerMid",
    "markerStart",
    "mask",
    "maskClip",
    "maskComposite",
    "maskImage",
    "maskMode",
    "maskOrigin",
    "maskPosition",
    "maskRepeat",
    "maskSize",
    "maskType",
    "mathDepth",
    "mathShift",
    "mathStyle",
    "maxBlockSize",
    "maxHeight",
    "maxInlineSize",
    "maxWidth",
    "minBlockSize",
    "minHeight",
    "minInlineSize",
    "minWidth",
    "mixBlendMode",
    "negative",
    "objectFit",
    "objectPosition",
    "objectViewBox",
    "offset",
    "offsetAnchor",
    "offsetDistance",
    "offsetPath",
    "offsetPosition",
    "offsetRotate",
    "opacity",
    "order",
    "orphans",
    "outline",
    "outlineColor",
    "outlineOffset",
    "outlineStyle",
    "outlineWidth",
    "overflow",
    "overflowAnchor",
    "overflowClipMargin",
    "overflowWrap",
    "overflowX",
    "overflowY",
    "overlay",
    "overrideColors",
    "overscrollBehavior",
    "overscrollBehaviorBlock",
    "overscrollBehaviorInline",
    "overscrollBehaviorX",
    "overscrollBehaviorY",
    "pad",
    "padding",
    "paddingBlock",
    "paddingBlockEnd",
    "paddingBlockStart",
    "paddingBottom",
    "paddingInline",
    "paddingInlineEnd",
    "paddingInlineStart",
    "paddingLeft",
    "paddingRight",
    "paddingTop",
    "page",
    "pageBreakAfter",
    "pageBreakBefore",
    "pageBreakInside",
    "pageOrientation",
    "paintOrder",
    "perspective",
    "perspectiveOrigin",
    "placeContent",
    "placeItems",
    "placeSelf",
    "pointerEvents",
    "position",
    "positionAnchor",
    "positionTry",
    "positionTryOptions",
    "positionTryOrder",
    "positionVisibility",
    "prefix",
    "quotes",
    "r",
    "range",
    "resize",
    "right",
    "rotate",
    "rowGap",
    "rubyPosition",
    "rx",
    "ry",
    "scale",
    "scrollBehavior",
    "scrollMargin",
    "scrollMarginBlock",
    "scrollMarginBlockEnd",
    "scrollMarginBlockStart",
    "scrollMarginBottom",
    "scrollMarginInline",
    "scrollMarginInlineEnd",
    "scrollMarginInlineStart",
    "scrollMarginLeft",
    "scrollMarginRight",
    "scrollMarginTop",
    "scrollPadding",
    "scrollPaddingBlock",
    "scrollPaddingBlockEnd",
    "scrollPaddingBlockStart",
    "scrollPaddingBottom",
    "scrollPaddingInline",
    "scrollPaddingInlineEnd",
    "scrollPaddingInlineStart",
    "scrollPaddingLeft",
    "scrollPaddingRight",
    "scrollPaddingTop",
    "scrollSnapAlign",
    "scrollSnapStop",
    "scrollSnapType",
    "scrollTimeline",
    "scrollTimelineAxis",
    "scrollTimelineName",
    "scrollbarColor",
    "scrollbarGutter",
    "scrollbarWidth",
    "shapeImageThreshold",
    "shapeMargin",
    "shapeOutside",
    "shapeRendering",
    "size",
    "sizeAdjust",
    "speak",
    "speakAs",
    "src",
    "stopColor",
    "stopOpacity",
    "stroke",
    "strokeDasharray",
    "strokeDashoffset",
    "strokeLinecap",
    "strokeLinejoin",
    "strokeMiterlimit",
    "strokeOpacity",
    "strokeWidth",
    "suffix",
    "symbols",
    "syntax",
    "system",
    "tabSize",
    "tableLayout",
    "textAlign",
    "textAlignLast",
    "textAnchor",
    "textCombineUpright",
    "textDecoration",
    "textDecorationColor",
    "textDecorationLine",
    "textDecorationSkipInk",
    "textDecorationStyle",
    "textDecorationThickness",
    "textEmphasis",
    "textEmphasisColor",
    "textEmphasisPosition",
    "textEmphasisStyle",
    "textIndent",
    "textOrientation",
    "textOverflow",
    "textRendering",
    "textShadow",
    "textSizeAdjust",
    "textSpacingTrim",
    "textTransform",
    "textUnderlineOffset",
    "textUnderlinePosition",
    "textWrap",
    "timelineScope",
    "top",
    "touchAction",
    "transform",
    "transformBox",
    "transformOrigin",
    "transformStyle",
    "transition",
    "transitionBehavior",
    "transitionDelay",
    "transitionDuration",
    "transitionProperty",
    "transitionTimingFunction",
    "translate",
    "unicodeBidi",
    "unicodeRange",
    "userSelect",
    "vectorEffect",
    "verticalAlign",
    "viewTimeline",
    "viewTimelineAxis",
    "viewTimelineInset",
    "viewTimelineName",
    "viewTransitionClass",
    "viewTransitionName",
    "visibility",
    "whiteSpace",
    "whiteSpaceCollapse",
    "widows",
    "width",
    "willChange",
    "wordBreak",
    "wordSpacing",
    "wordWrap",
    "writingMode",
    "x",
    "y",
    "zIndex",
    "zoom",
    "webkitAlignContent",
    "webkitAlignItems",
    "webkitAlignSelf",
    "webkitAnimation",
    "webkitAnimationDelay",
    "webkitAnimationDirection",
    "webkitAnimationDuration",
    "webkitAnimationFillMode",
    "webkitAnimationIterationCount",
    "webkitAnimationName",
    "webkitAnimationPlayState",
    "webkitAnimationTimingFunction",
    "webkitAppRegion",
    "webkitAppearance",
    "webkitBackfaceVisibility",
    "webkitBackgroundClip",
    "webkitBackgroundOrigin",
    "webkitBackgroundSize",
    "webkitBorderAfter",
    "webkitBorderAfterColor",
    "webkitBorderAfterStyle",
    "webkitBorderAfterWidth",
    "webkitBorderBefore",
    "webkitBorderBeforeColor",
    "webkitBorderBeforeStyle",
    "webkitBorderBeforeWidth",
    "webkitBorderBottomLeftRadius",
    "webkitBorderBottomRightRadius",
    "webkitBorderEnd",
    "webkitBorderEndColor",
    "webkitBorderEndStyle",
    "webkitBorderEndWidth",
    "webkitBorderHorizontalSpacing",
    "webkitBorderImage",
    "webkitBorderRadius",
    "webkitBorderStart",
    "webkitBorderStartColor",
    "webkitBorderStartStyle",
    "webkitBorderStartWidth",
    "webkitBorderTopLeftRadius",
    "webkitBorderTopRightRadius",
    "webkitBorderVerticalSpacing",
    "webkitBoxAlign",
    "webkitBoxDecorationBreak",
    "webkitBoxDirection",
    "webkitBoxFlex",
    "webkitBoxOrdinalGroup",
    "webkitBoxOrient",
    "webkitBoxPack",
    "webkitBoxReflect",
    "webkitBoxShadow",
    "webkitBoxSizing",
    "webkitClipPath",
    "webkitColumnBreakAfter",
    "webkitColumnBreakBefore",
    "webkitColumnBreakInside",
    "webkitColumnCount",
    "webkitColumnGap",
    "webkitColumnRule",
    "webkitColumnRuleColor",
    "webkitColumnRuleStyle",
    "webkitColumnRuleWidth",
    "webkitColumnSpan",
    "webkitColumnWidth",
    "webkitColumns",
    "webkitFilter",
    "webkitFlex",
    "webkitFlexBasis",
    "webkitFlexDirection",
    "webkitFlexFlow",
    "webkitFlexGrow",
    "webkitFlexShrink",
    "webkitFlexWrap",
    "webkitFontFeatureSettings",
    "webkitFontSmoothing",
    "webkitHyphenateCharacter",
    "webkitJustifyContent",
    "webkitLineBreak",
    "webkitLineClamp",
    "webkitLocale",
    "webkitLogicalHeight",
    "webkitLogicalWidth",
    "webkitMarginAfter",
    "webkitMarginBefore",
    "webkitMarginEnd",
    "webkitMarginStart",
    "webkitMask",
    "webkitMaskBoxImage",
    "webkitMaskBoxImageOutset",
    "webkitMaskBoxImageRepeat",
    "webkitMaskBoxImageSlice",
    "webkitMaskBoxImageSource",
    "webkitMaskBoxImageWidth",
    "webkitMaskClip",
    "webkitMaskComposite",
    "webkitMaskImage",
    "webkitMaskOrigin",
    "webkitMaskPosition",
    "webkitMaskPositionX",
    "webkitMaskPositionY",
    "webkitMaskRepeat",
    "webkitMaskSize",
    "webkitMaxLogicalHeight",
    "webkitMaxLogicalWidth",
    "webkitMinLogicalHeight",
    "webkitMinLogicalWidth",
    "webkitOpacity",
    "webkitOrder",
    "webkitPaddingAfter",
    "webkitPaddingBefore",
    "webkitPaddingEnd",
    "webkitPaddingStart",
    "webkitPerspective",
    "webkitPerspectiveOrigin",
    "webkitPerspectiveOriginX",
    "webkitPerspectiveOriginY",
    "webkitPrintColorAdjust",
    "webkitRtlOrdering",
    "webkitRubyPosition",
    "webkitShapeImageThreshold",
    "webkitShapeMargin",
    "webkitShapeOutside",
    "webkitTapHighlightColor",
    "webkitTextCombine",
    "webkitTextDecorationsInEffect",
    "webkitTextEmphasis",
    "webkitTextEmphasisColor",
    "webkitTextEmphasisPosition",
    "webkitTextEmphasisStyle",
    "webkitTextFillColor",
    "webkitTextOrientation",
    "webkitTextSecurity",
    "webkitTextSizeAdjust",
    "webkitTextStroke",
    "webkitTextStrokeColor",
    "webkitTextStrokeWidth",
    "webkitTransform",
    "webkitTransformOrigin",
    "webkitTransformOriginX",
    "webkitTransformOriginY",
    "webkitTransformOriginZ",
    "webkitTransformStyle",
    "webkitTransition",
    "webkitTransitionDelay",
    "webkitTransitionDuration",
    "webkitTransitionProperty",
    "webkitTransitionTimingFunction",
    "webkitUserDrag",
    "webkitUserModify",
    "webkitUserSelect",
    "webkitWritingMode",
};
static const int kCssComputedCamelCount = 629;

    // reference-measured Chrome CSS property name list (computed style
    // enumeration parity; the challenge SDK iterates cs[0..length))
    static const char* const kCssPropertyNames[] = {
    "accent-color",
    "align-content",
    "align-items",
    "align-self",
    "alignment-baseline",
    "anchor-name",
    "animation-composition",
    "animation-delay",
    "animation-direction",
    "animation-duration",
    "animation-fill-mode",
    "animation-iteration-count",
    "animation-name",
    "animation-play-state",
    "animation-range-end",
    "animation-range-start",
    "animation-timeline",
    "animation-timing-function",
    "app-region",
    "appearance",
    "backdrop-filter",
    "backface-visibility",
    "background-attachment",
    "background-blend-mode",
    "background-clip",
    "background-color",
    "background-image",
    "background-origin",
    "background-position",
    "background-repeat",
    "background-size",
    "baseline-shift",
    "baseline-source",
    "block-size",
    "border-block-end-color",
    "border-block-end-style",
    "border-block-end-width",
    "border-block-start-color",
    "border-block-start-style",
    "border-block-start-width",
    "border-bottom-color",
    "border-bottom-left-radius",
    "border-bottom-right-radius",
    "border-bottom-style",
    "border-bottom-width",
    "border-collapse",
    "border-end-end-radius",
    "border-end-start-radius",
    "border-image-outset",
    "border-image-repeat",
    "border-image-slice",
    "border-image-source",
    "border-image-width",
    "border-inline-end-color",
    "border-inline-end-style",
    "border-inline-end-width",
    "border-inline-start-color",
    "border-inline-start-style",
    "border-inline-start-width",
    "border-left-color",
    "border-left-style",
    "border-left-width",
    "border-right-color",
    "border-right-style",
    "border-right-width",
    "border-start-end-radius",
    "border-start-start-radius",
    "border-top-color",
    "border-top-left-radius",
    "border-top-right-radius",
    "border-top-style",
    "border-top-width",
    "bottom",
    "box-shadow",
    "box-sizing",
    "break-after",
    "break-before",
    "break-inside",
    "buffered-rendering",
    "caption-side",
    "caret-color",
    "clear",
    "clip",
    "clip-path",
    "clip-rule",
    "color",
    "color-interpolation",
    "color-interpolation-filters",
    "color-rendering",
    "column-count",
    "column-gap",
    "column-rule-color",
    "column-rule-style",
    "column-rule-width",
    "column-span",
    "column-width",
    "contain-intrinsic-block-size",
    "contain-intrinsic-height",
    "contain-intrinsic-inline-size",
    "contain-intrinsic-size",
    "contain-intrinsic-width",
    "container-name",
    "container-type",
    "content",
    "cursor",
    "cx",
    "cy",
    "d",
    "direction",
    "display",
    "dominant-baseline",
    "empty-cells",
    "field-sizing",
    "fill",
    "fill-opacity",
    "fill-rule",
    "filter",
    "flex-basis",
    "flex-direction",
    "flex-grow",
    "flex-shrink",
    "flex-wrap",
    "float",
    "flood-color",
    "flood-opacity",
    "font-family",
    "font-kerning",
    "font-optical-sizing",
    "font-palette",
    "font-size",
    "font-stretch",
    "font-style",
    "font-synthesis-small-caps",
    "font-synthesis-style",
    "font-synthesis-weight",
    "font-variant",
    "font-variant-alternates",
    "font-variant-caps",
    "font-variant-east-asian",
    "font-variant-ligatures",
    "font-variant-numeric",
    "font-variant-position",
    "font-weight",
    "grid-auto-columns",
    "grid-auto-flow",
    "grid-auto-rows",
    "grid-column-end",
    "grid-column-start",
    "grid-row-end",
    "grid-row-start",
    "grid-template-areas",
    "grid-template-columns",
    "grid-template-rows",
    "height",
    "hyphenate-character",
    "hyphenate-limit-chars",
    "hyphens",
    "image-orientation",
    "image-rendering",
    "initial-letter",
    "inline-size",
    "inset-area",
    "inset-block-end",
    "inset-block-start",
    "inset-inline-end",
    "inset-inline-start",
    "isolation",
    "justify-content",
    "justify-items",
    "justify-self",
    "left",
    "letter-spacing",
    "lighting-color",
    "line-break",
    "line-height",
    "list-style-image",
    "list-style-position",
    "list-style-type",
    "margin-block-end",
    "margin-block-start",
    "margin-bottom",
    "margin-inline-end",
    "margin-inline-start",
    "margin-left",
    "margin-right",
    "margin-top",
    "marker-end",
    "marker-mid",
    "marker-start",
    "mask-clip",
    "mask-composite",
    "mask-image",
    "mask-mode",
    "mask-origin",
    "mask-position",
    "mask-repeat",
    "mask-size",
    "mask-type",
    "math-depth",
    "math-shift",
    "math-style",
    "max-block-size",
    "max-height",
    "max-inline-size",
    "max-width",
    "min-block-size",
    "min-height",
    "min-inline-size",
    "min-width",
    "mix-blend-mode",
    "object-fit",
    "object-position",
    "object-view-box",
    "offset-anchor",
    "offset-distance",
    "offset-path",
    "offset-position",
    "offset-rotate",
    "opacity",
    "order",
    "orphans",
    "outline-color",
    "outline-offset",
    "outline-style",
    "outline-width",
    "overflow-anchor",
    "overflow-clip-margin",
    "overflow-wrap",
    "overflow-x",
    "overflow-y",
    "overlay",
    "overscroll-behavior-block",
    "overscroll-behavior-inline",
    "padding-block-end",
    "padding-block-start",
    "padding-bottom",
    "padding-inline-end",
    "padding-inline-start",
    "padding-left",
    "padding-right",
    "padding-top",
    "paint-order",
    "perspective",
    "perspective-origin",
    "pointer-events",
    "position",
    "position-anchor",
    "position-try-options",
    "position-try-order",
    "position-visibility",
    "r",
    "resize",
    "right",
    "rotate",
    "row-gap",
    "ruby-position",
    "rx",
    "ry",
    "scale",
    "scroll-behavior",
    "scroll-margin-block-end",
    "scroll-margin-block-start",
    "scroll-margin-inline-end",
    "scroll-margin-inline-start",
    "scroll-padding-block-end",
    "scroll-padding-block-start",
    "scroll-padding-inline-end",
    "scroll-padding-inline-start",
    "scroll-timeline-axis",
    "scroll-timeline-name",
    "scrollbar-color",
    "scrollbar-gutter",
    "scrollbar-width",
    "shape-image-threshold",
    "shape-margin",
    "shape-outside",
    "shape-rendering",
    "speak",
    "stop-color",
    "stop-opacity",
    "stroke",
    "stroke-dasharray",
    "stroke-dashoffset",
    "stroke-linecap",
    "stroke-linejoin",
    "stroke-miterlimit",
    "stroke-opacity",
    "stroke-width",
    "tab-size",
    "table-layout",
    "text-align",
    "text-align-last",
    "text-anchor",
    "text-decoration",
    "text-decoration-color",
    "text-decoration-line",
    "text-decoration-skip-ink",
    "text-decoration-style",
    "text-emphasis-color",
    "text-emphasis-position",
    "text-emphasis-style",
    "text-indent",
    "text-overflow",
    "text-rendering",
    "text-shadow",
    "text-size-adjust",
    "text-spacing-trim",
    "text-transform",
    "text-underline-position",
    "text-wrap",
    "timeline-scope",
    "top",
    "touch-action",
    "transform",
    "transform-origin",
    "transform-style",
    "transition-behavior",
    "transition-delay",
    "transition-duration",
    "transition-property",
    "transition-timing-function",
    "translate",
    "unicode-bidi",
    "user-select",
    "vector-effect",
    "vertical-align",
    "view-timeline-axis",
    "view-timeline-inset",
    "view-timeline-name",
    "view-transition-class",
    "view-transition-name",
    "visibility",
    "white-space-collapse",
    "widows",
    "width",
    "will-change",
    "word-break",
    "word-spacing",
    "writing-mode",
    "x",
    "y",
    "z-index",
    "zoom",
    "-webkit-border-horizontal-spacing",
    "-webkit-border-image",
    "-webkit-border-vertical-spacing",
    "-webkit-box-align",
    "-webkit-box-decoration-break",
    "-webkit-box-direction",
    "-webkit-box-flex",
    "-webkit-box-ordinal-group",
    "-webkit-box-orient",
    "-webkit-box-pack",
    "-webkit-box-reflect",
    "-webkit-font-smoothing",
    "-webkit-line-break",
    "-webkit-line-clamp",
    "-webkit-locale",
    "-webkit-mask-box-image",
    "-webkit-mask-box-image-outset",
    "-webkit-mask-box-image-repeat",
    "-webkit-mask-box-image-slice",
    "-webkit-mask-box-image-source",
    "-webkit-mask-box-image-width",
    "-webkit-print-color-adjust",
    "-webkit-rtl-ordering",
    "-webkit-tap-highlight-color",
    "-webkit-text-combine",
    "-webkit-text-decorations-in-effect",
    "-webkit-text-fill-color",
    "-webkit-text-orientation",
    "-webkit-text-security",
    "-webkit-text-stroke-color",
    "-webkit-text-stroke-width",
    "-webkit-user-drag",
    "-webkit-user-modify",
    "-webkit-writing-mode",
    };
    constexpr int kCssPropertyCount =
        static_cast<int>(sizeof(kCssPropertyNames) / sizeof(kCssPropertyNames[0]));

    void DataTransferItemListConstructor(
        const v8::FunctionCallbackInfo<v8::Value>& info)
    {
      v8::Isolate* isolate = info.GetIsolate();
      v8::Local<v8::Context> context = isolate->GetCurrentContext();
      ContextState* state = GetState(context);
      if (!info.IsConstructCall() || state == nullptr ||
          state->dtil_tpl.IsEmpty())
      {
        isolate->ThrowException(ToV8Str(
            isolate,
            "TypeError: DataTransferItemList constructor requires new"));
        return;
      }
      v8::Local<v8::Object> inst = state->dtil_tpl.Get(isolate)
                                       ->InstanceTemplate()
                                       ->NewInstance(context)
                                       .ToLocalChecked();
      v8::Local<v8::Value> undefined = v8::Undefined(isolate);
      v8::Local<v8::Value> owner =
          info.Length() >= 1 && info[0]->IsObject() ? info[0] : undefined;
      (void)inst->SetPrivate(context, DtilOwnerKey(isolate), owner)
          .FromMaybe(false);
      uint32_t count = 0;
      if (owner->IsObject())
        count = BuildItemsArray(isolate, context, owner.As<v8::Object>())
                    ->Length();
      (void)inst->Set(context, ToV8Str(isolate, "length"),
                      v8::Integer::New(isolate, static_cast<int32_t>(count)))
          .FromMaybe(false);
      info.GetReturnValue().Set(inst);
    }

    void DataTransferListItem(const v8::FunctionCallbackInfo<v8::Value>& info)
    {
      v8::Isolate* isolate = info.GetIsolate();
      v8::Local<v8::Context> context = isolate->GetCurrentContext();
      double index = info.Length() >= 1 ? NumOr(context, info[0], 0.0) : 0.0;
      v8::Local<v8::Value> item =
          info.This()->Get(context, index).FromMaybe(v8::Local<v8::Value>());
      if (item.IsEmpty() || !item->BooleanValue(isolate))
      {
        info.GetReturnValue().SetNull();
        return;
      }
      info.GetReturnValue().Set(item);
    }

    void DataTransferItemListAdd(
        const v8::FunctionCallbackInfo<v8::Value>& info)
    {
      v8::Isolate* isolate = info.GetIsolate();
      v8::Local<v8::Context> context = isolate->GetCurrentContext();
      ContextState* state = GetState(context);
      if (info.Length() < 1) return;
      v8::Local<v8::Value> owner;
      if (!info.This()->GetPrivate(context, DtilOwnerKey(isolate))
               .ToLocal(&owner) ||
          !owner->IsObject())
        return;
      v8::Local<v8::Object> owner_obj = owner.As<v8::Object>();
      if (state != nullptr && !state->file_tpl.IsEmpty() &&
          info[0]->IsObject() &&
          state->file_tpl.Get(isolate)->HasInstance(info[0].As<v8::Object>()))
      {
        v8::Local<v8::Value> files;
        if (owner_obj->GetPrivate(context, DtFilesKey(isolate))
                .ToLocal(&files) &&
            files->IsArray())
        {
          v8::Local<v8::Array> list = files.As<v8::Array>();
          (void)list->Set(context, list->Length(), info[0]).FromMaybe(false);
          v8::Local<v8::Value> type =
              owner_obj->Get(context, ToV8Str(isolate, "type"))
                  .FromMaybe(v8::Local<v8::Value>());
          v8::Local<v8::Value> file_type =
              info[0].As<v8::Object>()
                  ->Get(context, ToV8Str(isolate, "type"))
                  .FromMaybe(v8::Local<v8::Value>());
          info.GetReturnValue().Set(MakeDataTransferItem(
              isolate, context, "file", file_type, info[0]));
          return;
        }
        return;
      }
      v8::Local<v8::Value> undefined = v8::Undefined(isolate);
      v8::Local<v8::Value> key =
          info.Length() >= 2 ? info[1] : undefined;
      v8::Local<v8::Value> data = undefined;
      v8::Local<v8::Value> data_map;
      if (owner_obj->GetPrivate(context, DtDataKey(isolate)).ToLocal(&data_map) &&
          data_map->IsMap())
      {
        v8::Local<v8::Map> map = data_map.As<v8::Map>();
        if (!map->Set(context, key, info[0]).IsEmpty())
          data = map->Get(context, key).FromMaybe(undefined);
      }
      info.GetReturnValue().Set(MakeDataTransferItem(
          isolate, context, "string", key, data));
    }

    void DataTransferItemListRemove(
        const v8::FunctionCallbackInfo<v8::Value>& info)
    {
      v8::Isolate* isolate = info.GetIsolate();
      v8::Local<v8::Context> context = isolate->GetCurrentContext();
      if (info.Length() < 1) return;
      double index = NumOr(context, info[0], 0.0);
      v8::Local<v8::Value> item =
          info.This()->Get(context, index).FromMaybe(v8::Local<v8::Value>());
      if (item.IsEmpty() || !item->IsObject()) return;
      v8::Local<v8::Value> kind;
      if (!item.As<v8::Object>()
               ->Get(context, ToV8Str(isolate, "kind"))
               .ToLocal(&kind) ||
          !kind->IsString())
        return;
      v8::Local<v8::Value> owner;
      if (!info.This()->GetPrivate(context, DtilOwnerKey(isolate))
               .ToLocal(&owner) ||
          !owner->IsObject())
        return;
      v8::Local<v8::Object> owner_obj = owner.As<v8::Object>();
      std::string kind_text = ToUtf8(isolate, kind);
      if (kind_text == "string")
      {
        v8::Local<v8::Value> type;
        if (item.As<v8::Object>()
                ->Get(context, ToV8Str(isolate, "type"))
                .ToLocal(&type))
        {
          v8::Local<v8::Value> data_map;
          if (owner_obj->GetPrivate(context, DtDataKey(isolate))
                  .ToLocal(&data_map) &&
              data_map->IsMap())
            (void)data_map.As<v8::Map>()->Delete(context, type)
                .FromMaybe(false);
        }
      }
      else if (kind_text == "file")
      {
        v8::Local<v8::Value> value;
        v8::Local<v8::Value> files;
        if (item.As<v8::Object>()
                ->GetPrivate(context, DtiValueKey(isolate))
                .ToLocal(&value) &&
            owner_obj->GetPrivate(context, DtFilesKey(isolate))
                .ToLocal(&files) &&
            files->IsArray())
        {
          v8::Local<v8::Array> list = files.As<v8::Array>();
          for (uint32_t i = 0; i < list->Length(); i++)
          {
            v8::Local<v8::Value> candidate;
            if (list->Get(context, i).ToLocal(&candidate) &&
                candidate->SameValue(value))
            {
              for (uint32_t k = i; k + 1 < list->Length(); k++)
              {
                v8::Local<v8::Value> next;
                if (list->Get(context, k + 1).ToLocal(&next))
                  (void)list->Set(context, k, next).FromMaybe(false);
              }
              (void)list->Delete(context, list->Length() - 1)
                  .FromMaybe(false);
              break;
            }
          }
        }
      }
    }

    void DataTransferItemListClear(
        const v8::FunctionCallbackInfo<v8::Value>& info)
    {
      v8::Isolate* isolate = info.GetIsolate();
      v8::Local<v8::Context> context = isolate->GetCurrentContext();
      v8::Local<v8::Value> owner;
      if (!info.This()->GetPrivate(context, DtilOwnerKey(isolate))
               .ToLocal(&owner) ||
          !owner->IsObject())
        return;
      v8::Local<v8::Value> data_map;
      if (owner.As<v8::Object>()
              ->GetPrivate(context, DtDataKey(isolate))
              .ToLocal(&data_map) &&
          data_map->IsMap())
        data_map.As<v8::Map>()->Clear();
      (void)owner.As<v8::Object>()
          ->SetPrivate(context, DtFilesKey(isolate), v8::Array::New(isolate))
          .FromMaybe(false);
    }

    void DataTransferItemListIterator(
        const v8::FunctionCallbackInfo<v8::Value>& info)
    {
      v8::Isolate* isolate = info.GetIsolate();
      v8::Local<v8::Context> context = isolate->GetCurrentContext();
      v8::Local<v8::Value> owner;
      if (!info.This()->GetPrivate(context, DtilOwnerKey(isolate))
               .ToLocal(&owner) ||
          !owner->IsObject())
        return;
      v8::Local<v8::Array> items =
          BuildItemsArray(isolate, context, owner.As<v8::Object>());
      v8::Local<v8::Value> array_ctor;
      if (!context->Global()
               ->Get(context, ToV8Str(isolate, "Array"))
               .ToLocal(&array_ctor) ||
          !array_ctor->IsObject())
        return;
      v8::Local<v8::Value> array_proto;
      if (!array_ctor.As<v8::Object>()
               ->Get(context, ToV8Str(isolate, "prototype"))
               .ToLocal(&array_proto) ||
          !array_proto->IsObject())
        return;
      v8::Local<v8::Value> iterator_fn;
      if (!array_proto.As<v8::Object>()
               ->Get(context, v8::Symbol::GetIterator(isolate))
               .ToLocal(&iterator_fn) ||
          !iterator_fn->IsFunction())
        return;
      v8::Local<v8::Value> result =
          iterator_fn.As<v8::Function>()
              ->Call(context, items, 0, nullptr)
              .FromMaybe(v8::Local<v8::Value>());
      if (!result.IsEmpty())
        info.GetReturnValue().Set(result);
    }

    void DataTransferConstructor(
        const v8::FunctionCallbackInfo<v8::Value>& info)
    {
      v8::Isolate* isolate = info.GetIsolate();
      v8::Local<v8::Context> context = isolate->GetCurrentContext();
      ContextState* state = GetState(context);
      if (!info.IsConstructCall() || state == nullptr ||
          state->dt_tpl.IsEmpty())
      {
        isolate->ThrowException(ToV8Str(
            isolate, "TypeError: DataTransfer constructor requires new"));
        return;
      }
      v8::Local<v8::Object> inst = state->dt_tpl.Get(isolate)
                                       ->InstanceTemplate()
                                       ->NewInstance(context)
                                       .ToLocalChecked();
      // M11: dropEffect/effectAllowed are prototype accessors (face-table
      // constants "none"/"uninitialized"); instance-own data would pollute
      // the own-face (reference instances are zero-own)
      (void)inst->SetPrivate(context, DtDataKey(isolate),
                             v8::Map::New(isolate))
          .FromMaybe(false);
      (void)inst->SetPrivate(context, DtFilesKey(isolate),
                             v8::Array::New(isolate))
          .FromMaybe(false);
      info.GetReturnValue().Set(inst);
    }

    void DataTransferSetData(const v8::FunctionCallbackInfo<v8::Value>& info)
    {
      v8::Isolate* isolate = info.GetIsolate();
      v8::Local<v8::Context> context = isolate->GetCurrentContext();
      if (info.Length() < 2) return;
      v8::Local<v8::Value> data_map;
      if (info.This()
              ->GetPrivate(context, DtDataKey(isolate))
              .ToLocal(&data_map) &&
          data_map->IsMap())
      {
        (void)data_map.As<v8::Map>()
            ->Set(context, info[0], info[1])
            .IsEmpty();
      }
    }

    void DataTransferGetData(const v8::FunctionCallbackInfo<v8::Value>& info)
    {
      v8::Isolate* isolate = info.GetIsolate();
      v8::Local<v8::Context> context = isolate->GetCurrentContext();
      v8::Local<v8::Value> data_map;
      if (info.Length() >= 1 &&
          info.This()
              ->GetPrivate(context, DtDataKey(isolate))
              .ToLocal(&data_map) &&
          data_map->IsMap())
      {
        v8::Local<v8::Value> found =
            data_map.As<v8::Map>()
                ->Get(context, info[0])
                .FromMaybe(v8::Local<v8::Value>());
        if (!found.IsEmpty() && !found->IsUndefined())
        {
          info.GetReturnValue().Set(found);
          return;
        }
      }
      info.GetReturnValue().Set(ToV8Str(isolate, ""));
    }

    void DataTransferClearData(
        const v8::FunctionCallbackInfo<v8::Value>& info)
    {
      v8::Isolate* isolate = info.GetIsolate();
      v8::Local<v8::Context> context = isolate->GetCurrentContext();
      v8::Local<v8::Value> data_map;
      if (!info.This()
               ->GetPrivate(context, DtDataKey(isolate))
               .ToLocal(&data_map) ||
          !data_map->IsMap())
        return;
      if (info.Length() < 1 || info[0]->IsUndefined())
        data_map.As<v8::Map>()->Clear();
      else
        (void)data_map.As<v8::Map>()->Delete(context, info[0])
            .FromMaybe(false);
    }

    void DataTransferSetDragImage(
        const v8::FunctionCallbackInfo<v8::Value>& info)
    {
      // no-op (offline environment)
    }

    void DataTransferTypes(const v8::FunctionCallbackInfo<v8::Value>& info)
    {
      v8::Isolate* isolate = info.GetIsolate();
      v8::Local<v8::Context> context = isolate->GetCurrentContext();
      v8::Local<v8::Array> out = v8::Array::New(isolate);
      uint32_t n = 0;
      v8::Local<v8::Value> data_map;
      if (info.This()
              ->GetPrivate(context, DtDataKey(isolate))
              .ToLocal(&data_map) &&
          data_map->IsMap())
      {
        v8::Local<v8::Array> flat = data_map.As<v8::Map>()->AsArray();
        for (uint32_t i = 0; i + 1 < flat->Length(); i += 2)
        {
          v8::Local<v8::Value> key;
          if (flat->Get(context, i).ToLocal(&key))
            (void)out->Set(context, n++, key).FromMaybe(false);
        }
      }
      v8::Local<v8::Value> files;
      if (info.This()
              ->GetPrivate(context, DtFilesKey(isolate))
              .ToLocal(&files) &&
          files->IsArray() &&
          files.As<v8::Array>()->Length() > 0)
        (void)out->Set(context, n++, ToV8Str(isolate, "Files"))
            .FromMaybe(false);
      info.GetReturnValue().Set(out);
    }

    void DataTransferFiles(const v8::FunctionCallbackInfo<v8::Value>& info)
    {
      v8::Isolate* isolate = info.GetIsolate();
      v8::Local<v8::Context> context = isolate->GetCurrentContext();
      v8::Local<v8::Value> files;
      if (info.This()
              ->GetPrivate(context, DtFilesKey(isolate))
              .ToLocal(&files) &&
          files->IsArray())
      {
        info.GetReturnValue().Set(
            MakeFileList(isolate, context, files.As<v8::Array>()));
        return;
      }
      info.GetReturnValue().Set(
          MakeFileList(isolate, context, v8::Array::New(isolate)));
    }

    void DataTransferItems(const v8::FunctionCallbackInfo<v8::Value>& info)
    {
      v8::Isolate* isolate = info.GetIsolate();
      v8::Local<v8::Context> context = isolate->GetCurrentContext();
      ContextState* state = GetState(context);
      if (state == nullptr || state->dtil_tpl.IsEmpty()) return;
      v8::Local<v8::Object> inst = state->dtil_tpl.Get(isolate)
                                       ->InstanceTemplate()
                                       ->NewInstance(context)
                                       .ToLocalChecked();
      (void)inst->SetPrivate(context, DtilOwnerKey(isolate), info.This())
          .FromMaybe(false);
      v8::Local<v8::Array> items =
          BuildItemsArray(isolate, context, info.This());
      uint32_t count = items->Length();
      for (uint32_t i = 0; i < count; i++)
      {
        v8::Local<v8::Value> item;
        if (items->Get(context, i).ToLocal(&item))
          (void)inst->Set(context, i, item).FromMaybe(false);
      }
      (void)inst->Set(context, ToV8Str(isolate, "length"),
                      v8::Integer::New(isolate, static_cast<int32_t>(count)))
          .FromMaybe(false);
      info.GetReturnValue().Set(inst);
    }
  }  // namespace (geometry + DataTransfer helpers)

    // ---- G4 ledger chunk 2: CSS Typed OM (CSSKeywordValue /
    // CSSUnitValue / CSS namespace). Previously JS bootstrap classes
    // (polyfills3); the element-bound style views (styleDeclarations /
    // styleMap) stay on the JS registration surface by design.

    v8::Local<v8::Object> MakeKeywordValue(v8::Isolate* isolate,
                                           v8::Local<v8::Context> context,
                                           v8::Local<v8::Value> value)
    {
      ContextState* state = GetState(context);
      v8::Local<v8::Object> inst = state->css_keyword_tpl.Get(isolate)
                                       ->InstanceTemplate()
                                       ->NewInstance(context)
                                       .ToLocalChecked();
      v8::Local<v8::String> text;
      if (!value->ToString(context).ToLocal(&text))
        text = ToV8Str(isolate, "undefined");
      (void)inst->Set(context, ToV8Str(isolate, "value"), text)
          .FromMaybe(false);
      return inst;
    }

    void KeywordValueConstructor(
        const v8::FunctionCallbackInfo<v8::Value>& info)
    {
      v8::Isolate* isolate = info.GetIsolate();
      v8::Local<v8::Context> context = isolate->GetCurrentContext();
      ContextState* state = GetState(context);
      if (!info.IsConstructCall() || state == nullptr ||
          state->css_keyword_tpl.IsEmpty())
      {
        isolate->ThrowException(ToV8Str(
            isolate, "TypeError: CSSKeywordValue constructor requires new"));
        return;
      }
      info.GetReturnValue().Set(MakeKeywordValue(
          isolate, context,
          info.Length() >= 1
              ? info[0]
              : v8::Local<v8::Value>(v8::Undefined(isolate))));
    }

    void KeywordValueToString(
        const v8::FunctionCallbackInfo<v8::Value>& info)
    {
      v8::Isolate* isolate = info.GetIsolate();
      v8::Local<v8::Context> context = isolate->GetCurrentContext();
      info.GetReturnValue().Set(
          info.This()->Get(context, ToV8Str(isolate, "value"))
              .FromMaybe(v8::Local<v8::Value>(v8::Undefined(isolate))));
    }

    v8::Local<v8::Object> MakeUnitValue(v8::Isolate* isolate,
                                        v8::Local<v8::Context> context,
                                        v8::Local<v8::Value> value,
                                        v8::Local<v8::Value> unit)
    {
      ContextState* state = GetState(context);
      v8::Local<v8::Object> inst = state->css_unit_tpl.Get(isolate)
                                       ->InstanceTemplate()
                                       ->NewInstance(context)
                                       .ToLocalChecked();
      (void)inst
          ->Set(context, ToV8Str(isolate, "value"),
                v8::Number::New(isolate,
                                NumOr(context, value,
                                      std::numeric_limits<double>::quiet_NaN())))
          .FromMaybe(false);
      v8::Local<v8::String> text;
      if (unit.IsEmpty() || !unit->ToString(context).ToLocal(&text))
        text = ToV8Str(isolate, "undefined");
      (void)inst->Set(context, ToV8Str(isolate, "unit"), text)
          .FromMaybe(false);
      return inst;
    }

    void UnitValueConstructor(
        const v8::FunctionCallbackInfo<v8::Value>& info)
    {
      v8::Isolate* isolate = info.GetIsolate();
      v8::Local<v8::Context> context = isolate->GetCurrentContext();
      ContextState* state = GetState(context);
      if (!info.IsConstructCall() || state == nullptr ||
          state->css_unit_tpl.IsEmpty())
      {
        isolate->ThrowException(ToV8Str(
            isolate, "TypeError: CSSUnitValue constructor requires new"));
        return;
      }
      v8::Local<v8::Value> undefined = v8::Undefined(isolate);
      info.GetReturnValue().Set(MakeUnitValue(
          isolate, context,
          info.Length() >= 1 ? info[0] : undefined,
          info.Length() >= 2 ? info[1] : undefined));
    }

    // JS: this.value + this.unit -- ToString of the number gives the
    // ECMAScript number formatting (24 -> "24", 0.5 -> "0.5")
    void UnitValueToString(const v8::FunctionCallbackInfo<v8::Value>& info)
    {
      v8::Isolate* isolate = info.GetIsolate();
      v8::Local<v8::Context> context = isolate->GetCurrentContext();
      v8::Local<v8::Value> value, unit;
      if (!info.This()
               ->Get(context, ToV8Str(isolate, "value"))
               .ToLocal(&value) ||
          !info.This()
               ->Get(context, ToV8Str(isolate, "unit"))
               .ToLocal(&unit))
        return;
      v8::Local<v8::String> value_text;
      if (!value->ToString(context).ToLocal(&value_text)) return;
      v8::Local<v8::String> unit_text;
      if (!unit->ToString(context).ToLocal(&unit_text)) return;
      info.GetReturnValue().Set(
          v8::String::Concat(isolate, value_text, unit_text));
    }

    void CssPx(const v8::FunctionCallbackInfo<v8::Value>& info)
    {
      v8::Isolate* isolate = info.GetIsolate();
      v8::Local<v8::Context> context = isolate->GetCurrentContext();
      info.GetReturnValue().Set(MakeUnitValue(
          isolate, context,
          info.Length() >= 1
              ? info[0]
              : v8::Local<v8::Value>(v8::Undefined(isolate)),
          ToV8Str(isolate, "px")));
    }

    void CssDeg(const v8::FunctionCallbackInfo<v8::Value>& info)
    {
      v8::Isolate* isolate = info.GetIsolate();
      v8::Local<v8::Context> context = isolate->GetCurrentContext();
      info.GetReturnValue().Set(MakeUnitValue(
          isolate, context,
          info.Length() >= 1
              ? info[0]
              : v8::Local<v8::Value>(v8::Undefined(isolate)),
          ToV8Str(isolate, "deg")));
    }

    void CssNumber(const v8::FunctionCallbackInfo<v8::Value>& info)
    {
      v8::Isolate* isolate = info.GetIsolate();
      v8::Local<v8::Context> context = isolate->GetCurrentContext();
      info.GetReturnValue().Set(MakeUnitValue(
          isolate, context,
          info.Length() >= 1
              ? info[0]
              : v8::Local<v8::Value>(v8::Undefined(isolate)),
          ToV8Str(isolate, "number")));
    }

    void CssKeyword(const v8::FunctionCallbackInfo<v8::Value>& info)
    {
      v8::Isolate* isolate = info.GetIsolate();
      v8::Local<v8::Context> context = isolate->GetCurrentContext();
      info.GetReturnValue().Set(MakeKeywordValue(
          isolate, context,
          info.Length() >= 1
              ? info[0]
              : v8::Local<v8::Value>(v8::Undefined(isolate))));
    }

    void CssSupports(const v8::FunctionCallbackInfo<v8::Value>& info)
    {
      v8::Isolate* isolate = info.GetIsolate();
      v8::Local<v8::Context> context = isolate->GetCurrentContext();
      static const char* const supported[] = {
          "display", "color", "width", "height", "transform", "opacity",
          "position", "grid", "flex"};
      std::string prop;
      if (info.Length() >= 1)
      {
        prop = ToUtf8(isolate, info[0]);
        for (char& c : prop)
          c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
      }
      for (const char* name : supported)
      {
        if (prop == name)
        {
          info.GetReturnValue().Set(v8::Boolean::New(isolate, true));
          return;
        }
      }
      info.GetReturnValue().Set(v8::Boolean::New(isolate, false));
    }

    void InstallCssTypedOm(v8::Isolate* isolate,
                           v8::Local<v8::Context> context)
    {
      ContextState* state = GetState(context);
      v8::Local<v8::Object> global = context->Global();
      auto register_global = [&](const char* name, v8::Local<v8::Value> v) {
        (void)global
            ->DefineOwnProperty(context, ToV8Str(isolate, name), v,
                                static_cast<v8::PropertyAttribute>(v8::PropertyAttribute::DontEnum))
            .FromMaybe(false);
      };

      if (state->css_keyword_tpl.IsEmpty())
      {
        v8::Local<v8::FunctionTemplate> ctor = v8::FunctionTemplate::New(
            isolate, KeywordValueConstructor);
        ctor->SetClassName(ToV8Str(isolate, "CSSKeywordValue"));
        v8::Local<v8::ObjectTemplate> inst = ctor->InstanceTemplate();
        inst->SetInternalFieldCount(0);
        // M10: zero own symbols on instances (tag on prototype)
        ctor->PrototypeTemplate()
            ->Set(v8::Symbol::GetToStringTag(isolate),
                  ToV8Str(isolate, "CSSKeywordValue"),
                  static_cast<v8::PropertyAttribute>(
                      v8::PropertyAttribute::ReadOnly |
                      v8::PropertyAttribute::DontEnum));
        v8::Local<v8::FunctionTemplate> to_string =
            v8::FunctionTemplate::New(isolate, KeywordValueToString);
        to_string->SetClassName(ToV8Str(isolate, "toString"));
        ctor->PrototypeTemplate()->Set(ToV8Str(isolate, "toString"),
                                       to_string);
        state->css_keyword_tpl.Reset(isolate, ctor);
        register_global("CSSKeywordValue",
                        ctor->GetFunction(context).ToLocalChecked());
      }

      if (state->css_unit_tpl.IsEmpty())
      {
        v8::Local<v8::FunctionTemplate> ctor = v8::FunctionTemplate::New(
            isolate, UnitValueConstructor);
        ctor->SetClassName(ToV8Str(isolate, "CSSUnitValue"));
        v8::Local<v8::ObjectTemplate> inst = ctor->InstanceTemplate();
        inst->SetInternalFieldCount(0);
        // M10: zero own symbols on instances (tag on prototype)
        ctor->PrototypeTemplate()
            ->Set(v8::Symbol::GetToStringTag(isolate),
                  ToV8Str(isolate, "CSSUnitValue"),
                  static_cast<v8::PropertyAttribute>(
                      v8::PropertyAttribute::ReadOnly |
                      v8::PropertyAttribute::DontEnum));
        v8::Local<v8::FunctionTemplate> to_string =
            v8::FunctionTemplate::New(isolate, UnitValueToString);
        to_string->SetClassName(ToV8Str(isolate, "toString"));
        ctor->PrototypeTemplate()->Set(ToV8Str(isolate, "toString"),
                                       to_string);
        state->css_unit_tpl.Reset(isolate, ctor);
        register_global("CSSUnitValue",
                        ctor->GetFunction(context).ToLocalChecked());
      }

      if (state->css_keyword_tpl.IsEmpty() || state->css_unit_tpl.IsEmpty())
        return;
      v8::Local<v8::Object> css = v8::Object::New(isolate);
      auto factory = [&](const char* name, v8::FunctionCallback cb) {
        v8::Local<v8::String> key = ToV8Str(isolate, name);
        v8::Local<v8::FunctionTemplate> fn =
            v8::FunctionTemplate::New(isolate, cb);
        fn->SetClassName(key);
        (void)css->Set(context, key,
                       fn->GetFunction(context).ToLocalChecked())
            .FromMaybe(false);
      };
      factory("px", CssPx);
      factory("deg", CssDeg);
      factory("number", CssNumber);
      factory("keyword", CssKeyword);
      // reference's CSS namespace has no supports() (reference-measured)
      register_global("CSS", css);
    }

    // ---- G4 ledger chunk 7a: web audio value layer ---------------------
    // (AudioBuffer/AudioNode/BaseAudioContext/AudioContext/
    // OfflineAudioContext), formerly JS bootstrap classes. Deterministic,
    // environment-driven: sampleRate/maxChannelCount come from
    // __LV8_PROFILE__.audioContext || .audio.

    double AudioProfileValue(v8::Isolate* isolate,
                             v8::Local<v8::Context> context,
                             const char* key, double fallback)
    {
      v8::Local<v8::Value> profile;
      if (!context->Global()
                ->Get(context, ToV8Str(isolate, "__LV8_PROFILE__"))
                .ToLocal(&profile) ||
          !profile->IsObject())
        return fallback;
      v8::Local<v8::Value> group;
      if (profile.As<v8::Object>()
              ->Get(context, ToV8Str(isolate, "audioContext"))
              .ToLocal(&group) &&
          group->IsObject())
      {
        v8::Local<v8::Value> value;
        if (group.As<v8::Object>()
                ->Get(context, ToV8Str(isolate, key))
                .ToLocal(&value) &&
            value->IsNumber())
          return value->NumberValue(context).FromMaybe(fallback);
      }
      if (profile.As<v8::Object>()
              ->Get(context, ToV8Str(isolate, "audio"))
              .ToLocal(&group) &&
          group->IsObject())
      {
        v8::Local<v8::Value> value;
        if (group.As<v8::Object>()
                ->Get(context, ToV8Str(isolate, key))
                .ToLocal(&value) &&
            value->IsNumber())
          return value->NumberValue(context).FromMaybe(fallback);
      }
      return fallback;
    }

    v8::Local<v8::Object> MakeAudioBuffer(v8::Isolate* isolate,
                                          v8::Local<v8::Context> context,
                                          double channels, double length,
                                          double sample_rate)
    {
      ContextState* state = GetState(context);
      if (sample_rate == 0)
        sample_rate = AudioProfileValue(isolate, context, "sampleRate", 48000);
      if (length == 0) length = 1;
      v8::Local<v8::Object> inst = state->audiobuffer_tpl.Get(isolate)
                                       ->InstanceTemplate()
                                       ->NewInstance(context)
                                       .ToLocalChecked();
      (void)inst->Set(context, ToV8Str(isolate, "sampleRate"),
                      v8::Number::New(isolate, sample_rate))
          .FromMaybe(false);
      (void)inst->Set(context, ToV8Str(isolate, "length"),
                      v8::Number::New(isolate, length))
          .FromMaybe(false);
      (void)inst->Set(context, ToV8Str(isolate, "numberOfChannels"),
                      v8::Number::New(isolate, channels))
          .FromMaybe(false);
      (void)inst->Set(context, ToV8Str(isolate, "duration"),
                      v8::Number::New(isolate, length / sample_rate))
          .FromMaybe(false);
      v8::Local<v8::Array> channel_list = v8::Array::New(isolate);
      for (int32_t i = 0; i < static_cast<int32_t>(channels); i++)
      {
        v8::Local<v8::ArrayBuffer> buffer =
            v8::ArrayBuffer::New(isolate, static_cast<size_t>(length) * 4);
        v8::Local<v8::Float32Array> data =
            v8::Float32Array::New(buffer, 0, static_cast<size_t>(length));
        (void)channel_list->Set(context, static_cast<uint32_t>(i), data)
            .FromMaybe(false);
      }
      (void)inst->Set(context, ToV8Str(isolate, "_channels"), channel_list)
          .FromMaybe(false);
      return inst;
    }

    void AudioBufferConstructor(
        const v8::FunctionCallbackInfo<v8::Value>& info)
    {
      v8::Isolate* isolate = info.GetIsolate();
      v8::Local<v8::Context> context = isolate->GetCurrentContext();
      ContextState* state = GetState(context);
      if (!info.IsConstructCall() || state == nullptr ||
          state->audiobuffer_tpl.IsEmpty())
      {
        isolate->ThrowException(ToV8Str(
            isolate, "TypeError: AudioBuffer constructor requires new"));
        return;
      }
      double sample_rate = AudioProfileValue(isolate, context, "sampleRate",
                                             48000);
      double length = 1;
      double channels = 1;
      if (info.Length() >= 1 && info[0]->IsObject())
      {
        v8::Local<v8::Object> options = info[0].As<v8::Object>();
        v8::Local<v8::Value> raw;
        if (options->Get(context, ToV8Str(isolate, "sampleRate"))
                .ToLocal(&raw) &&
            raw->IsNumber())
          sample_rate = raw->NumberValue(context).FromMaybe(sample_rate);
        if (options->Get(context, ToV8Str(isolate, "length"))
                .ToLocal(&raw) &&
            raw->IsNumber())
          length = raw->NumberValue(context).FromMaybe(1);
        if (options->Get(context, ToV8Str(isolate, "numberOfChannels"))
                .ToLocal(&raw) &&
            raw->IsNumber())
          channels = raw->NumberValue(context).FromMaybe(1);
      }
      info.GetReturnValue().Set(
          MakeAudioBuffer(isolate, context, channels, length, sample_rate));
    }

    void AudioBufferGetChannelData(
        const v8::FunctionCallbackInfo<v8::Value>& info)
    {
      v8::Isolate* isolate = info.GetIsolate();
      v8::Local<v8::Context> context = isolate->GetCurrentContext();
      v8::Local<v8::Value> channels;
      if (!info.This()
               ->Get(context, ToV8Str(isolate, "_channels"))
               .ToLocal(&channels) ||
          !channels->IsArray())
        return;
      double index = info.Length() >= 1
          ? info[0]->NumberValue(context).FromMaybe(0.0)
          : 0.0;
      v8::Local<v8::Array> list = channels.As<v8::Array>();
      uint32_t i = index >= 0 ? static_cast<uint32_t>(index) : 0;
      v8::Local<v8::Value> data;
      if (list->Get(context, i).ToLocal(&data) && !data->IsUndefined())
      {
        info.GetReturnValue().Set(data);
        return;
      }
      v8::Local<v8::Value> first;
      if (list->Get(context, 0).ToLocal(&first))
        info.GetReturnValue().Set(first);
    }

    void AudioNodeConstructor(const v8::FunctionCallbackInfo<v8::Value>& info)
    {
      v8::Isolate* isolate = info.GetIsolate();
      // reference gold: AudioNode is not constructible at all
      isolate->ThrowException(
          ToV8Str(isolate, "Illegal constructor"));
    }

    void AudioNodeConnect(const v8::FunctionCallbackInfo<v8::Value>& info)
    {
      v8::Isolate* isolate = info.GetIsolate();
      v8::Local<v8::Context> context = isolate->GetCurrentContext();
      if (info.Length() < 1) return;
      v8::Local<v8::Value> connections;
      if (!info.This()
               ->Get(context, ToV8Str(isolate, "_connections"))
               .ToLocal(&connections) ||
          !connections->IsArray())
        return;
      v8::Local<v8::Array> list = connections.As<v8::Array>();
      (void)list->Set(context, list->Length(), info[0]).FromMaybe(false);
      info.GetReturnValue().Set(info[0]);
    }

    void AudioNodeDisconnect(const v8::FunctionCallbackInfo<v8::Value>& info)
    {
      v8::Isolate* isolate = info.GetIsolate();
      v8::Local<v8::Context> context = isolate->GetCurrentContext();
      (void)info.This()->Set(context, ToV8Str(isolate, "_connections"),
                             v8::Array::New(isolate))
          .FromMaybe(false);
    }

    void AudioNodeStart(const v8::FunctionCallbackInfo<v8::Value>& info) {}
    void AudioNodeStop(const v8::FunctionCallbackInfo<v8::Value>& info) {}

    void InitAudioContextBase(v8::Isolate* isolate,
                              v8::Local<v8::Context> context,
                              v8::Local<v8::Object> inst,
                              double sample_rate)
    {
      if (sample_rate == 0)
        sample_rate = AudioProfileValue(isolate, context, "sampleRate", 48000);
      (void)inst->Set(context, ToV8Str(isolate, "sampleRate"),
                      v8::Number::New(isolate, sample_rate))
          .FromMaybe(false);
      // reference gold: destination exists but is null, state is "suspended"
      (void)inst->Set(context, ToV8Str(isolate, "destination"),
                      v8::Null(isolate))
          .FromMaybe(false);
      (void)inst->Set(context, ToV8Str(isolate, "currentTime"),
                      v8::Number::New(isolate, 0))
          .FromMaybe(false);
      (void)inst->Set(context, ToV8Str(isolate, "state"),
                      ToV8Str(isolate, "suspended"))
          .FromMaybe(false);
      // reference gold: event handler slot exists and reads null
      (void)inst->Set(context, ToV8Str(isolate, "onstatechange"),
                      v8::Null(isolate))
          .FromMaybe(false);
    }

    void BaseAudioContextConstructor(
        const v8::FunctionCallbackInfo<v8::Value>& info)
    {
      v8::Isolate* isolate = info.GetIsolate();
      // reference gold: BaseAudioContext is not constructible at all
      isolate->ThrowException(
          ToV8Str(isolate, "Illegal constructor"));
    }

    // reference gold: every node factory returns null, control methods return
    // never-settling promises (its rendering/decode/resume promises stay
    // pending), and getOutputTimestamp is a plain zeroed reading.
    void AudioContextCreateNull(
        const v8::FunctionCallbackInfo<v8::Value>& info)
    {
      info.GetReturnValue().SetNull();
    }

    void AudioContextPendingPromise(
        const v8::FunctionCallbackInfo<v8::Value>& info)
    {
      v8::Isolate* isolate = info.GetIsolate();
      v8::Local<v8::Context> context = isolate->GetCurrentContext();
      v8::Local<v8::Promise::Resolver> resolver =
          v8::Promise::Resolver::New(context).ToLocalChecked();
      info.GetReturnValue().Set(resolver->GetPromise());
    }

    v8::Local<v8::Private> AudioPrivateKey(v8::Isolate* isolate,
                                            const char* name)
    {
      return v8::Private::ForApi(isolate,
          ToV8Str(isolate, std::string("lv8.audio.") + name));
    }

    void AudioFixedPropertyGetter(
        const v8::FunctionCallbackInfo<v8::Value>& info)
    {
      v8::Isolate* isolate = info.GetIsolate();
      const std::string name = ToUtf8(isolate, info.Data());
      if (name == "baseLatency")
        info.GetReturnValue().Set(v8::Number::New(isolate, 0.01));
      else if (name == "outputLatency")
        info.GetReturnValue().Set(v8::Number::New(isolate, 0.02));
      else if (name == "sinkId")
        info.GetReturnValue().SetEmptyString();
    }

    void AudioLengthGetter(const v8::FunctionCallbackInfo<v8::Value>& info)
    {
      v8::Isolate* isolate = info.GetIsolate();
      v8::Local<v8::Context> context = isolate->GetCurrentContext();
      v8::Local<v8::Value> value;
      if (info.This()->GetPrivate(context, AudioPrivateKey(isolate, "length"))
              .ToLocal(&value) && value->IsNumber())
      {
        info.GetReturnValue().Set(value);
        return;
      }
      info.GetReturnValue().Set(v8::Number::New(isolate, 1));
    }

    void AudioEventSlotGetter(const v8::FunctionCallbackInfo<v8::Value>& info)
    {
      v8::Isolate* isolate = info.GetIsolate();
      v8::Local<v8::Context> context = isolate->GetCurrentContext();
      const std::string name = ToUtf8(isolate, info.Data());
      v8::Local<v8::Value> value;
      if (info.This()->GetPrivate(context, AudioPrivateKey(isolate, name.c_str()))
              .ToLocal(&value))
      {
        info.GetReturnValue().Set(value);
        return;
      }
      info.GetReturnValue().SetNull();
    }

    void AudioEventSlotSetter(const v8::FunctionCallbackInfo<v8::Value>& info)
    {
      if (info.Length() < 1) return;
      v8::Isolate* isolate = info.GetIsolate();
      const std::string name = ToUtf8(isolate, info.Data());
      (void)info.This()->SetPrivate(isolate->GetCurrentContext(),
          AudioPrivateKey(isolate, name.c_str()), info[0]).FromMaybe(false);
    }

    // G4.285: numeric accessor backed by the audio private store, with the
    // reference's per-name fallbacks (sampleRate / channelCount / ...).
    void AudioNumberGetter(const v8::FunctionCallbackInfo<v8::Value>& info)
    {
      v8::Isolate* isolate = info.GetIsolate();
      v8::Local<v8::Context> context = isolate->GetCurrentContext();
      const std::string name = ToUtf8(isolate, info.Data());
      v8::Local<v8::Value> value;
      if (info.This()
              ->GetPrivate(context, AudioPrivateKey(isolate, name.c_str()))
              .ToLocal(&value) &&
          value->IsNumber())
      {
        info.GetReturnValue().Set(value);
        return;
      }
      // own data properties (the JS/constructor path stores them there)
      v8::Local<v8::String> own_key = ToV8Str(isolate, name);
      if (info.This()->HasOwnProperty(context, own_key).FromMaybe(false) &&
          info.This()->Get(context, own_key).ToLocal(&value) &&
          value->IsNumber())
      {
        info.GetReturnValue().Set(value);
        return;
      }
      double fallback = 0;
      if (name == "sampleRate")
        fallback = AudioProfileValue(isolate, context, "sampleRate", 48000);
      else if (name == "channelCount")
        fallback = 2;
      else if (name == "numberOfInputs" || name == "numberOfOutputs")
        fallback = 1;
      info.GetReturnValue().Set(v8::Number::New(isolate, fallback));
    }

    // String-valued private slot with the reference's defaults
    // (channelCountMode="max", channelInterpretation="speakers",
    // state="running"); falls back to null for unknown names.
    void AudioSlotGetter(const v8::FunctionCallbackInfo<v8::Value>& info)
    {
      v8::Isolate* isolate = info.GetIsolate();
      v8::Local<v8::Context> context = isolate->GetCurrentContext();
      const std::string name = ToUtf8(isolate, info.Data());
      v8::Local<v8::Value> value;
      if (info.This()
              ->GetPrivate(context, AudioPrivateKey(isolate, name.c_str()))
              .ToLocal(&value) &&
          !value->IsNullOrUndefined())
      {
        info.GetReturnValue().Set(value);
        return;
      }
      v8::Local<v8::String> own_key = ToV8Str(isolate, name);
      if (info.This()->HasOwnProperty(context, own_key).FromMaybe(false) &&
          info.This()->Get(context, own_key).ToLocal(&value) &&
          !value->IsNullOrUndefined())
      {
        info.GetReturnValue().Set(value);
        return;
      }
      if (name == "channelCountMode")
        info.GetReturnValue().Set(ToV8Str(isolate, "max"));
      else if (name == "channelInterpretation")
        info.GetReturnValue().Set(ToV8Str(isolate, "speakers"));
      else if (name == "state")
        info.GetReturnValue().Set(ToV8Str(isolate, "running"));
      else
        info.GetReturnValue().SetNull();
    }

    // Minimal object-valued accessors (audioWorklet / destination /
    // listener).  The reference exposes them as plain accessors; the values
    // are intentionally inert - lv8 does not run a real audio graph.
    void AudioInertObjectGetter(const v8::FunctionCallbackInfo<v8::Value>& info)
    {
      v8::Isolate* isolate = info.GetIsolate();
      v8::Local<v8::Context> context = isolate->GetCurrentContext();
      const std::string name = ToUtf8(isolate, info.Data());
      v8::Local<v8::Value> value;
      if (info.This()
              ->GetPrivate(context, AudioPrivateKey(isolate, name.c_str()))
              .ToLocal(&value) &&
          value->IsObject())
      {
        info.GetReturnValue().Set(value);
        return;
      }
      v8::Local<v8::Object> obj = v8::Object::New(isolate);
      if (name == "audioWorklet")
      {
        v8::Local<v8::FunctionTemplate> add_module =
            v8::FunctionTemplate::New(isolate, AudioContextPendingPromise);
        (void)obj->Set(context, ToV8Str(isolate, "addModule"),
                       add_module->GetFunction(context).ToLocalChecked())
            .FromMaybe(false);
      }
      (void)info.This()
          ->SetPrivate(context, AudioPrivateKey(isolate, name.c_str()), obj)
          .FromMaybe(false);
      info.GetReturnValue().Set(obj);
    }

    void AudioBufferCopyChannel(const v8::FunctionCallbackInfo<v8::Value>& info)
    {
      (void)info;
    }

    void AudioContextGetOutputTimestamp(
        const v8::FunctionCallbackInfo<v8::Value>& info)
    {
      v8::Isolate* isolate = info.GetIsolate();
      v8::Local<v8::Context> context = isolate->GetCurrentContext();
      v8::Local<v8::Object> ts = v8::Object::New(isolate);
      (void)ts->Set(context, ToV8Str(isolate, "contextTime"),
                    v8::Number::New(isolate, 0))
          .FromMaybe(false);
      (void)ts->Set(context, ToV8Str(isolate, "performanceTime"),
                    v8::Number::New(isolate, 0))
          .FromMaybe(false);
      info.GetReturnValue().Set(ts);
    }

    void AudioParamConstructor(
        const v8::FunctionCallbackInfo<v8::Value>& info)
    {
      v8::Isolate* isolate = info.GetIsolate();
      isolate->ThrowException(
          ToV8Str(isolate, "Illegal constructor"));
    }

    void AudioContextCreateOscillator(
        const v8::FunctionCallbackInfo<v8::Value>& info)
    {
      info.GetReturnValue().SetNull();
    }

    void AudioContextCreateGain(
        const v8::FunctionCallbackInfo<v8::Value>& info)
    {
      info.GetReturnValue().SetNull();
    }

    void AudioContextCreateBuffer(
        const v8::FunctionCallbackInfo<v8::Value>& info)
    {
      info.GetReturnValue().SetNull();
    }

    void AudioContextDecodeAudioData(
        const v8::FunctionCallbackInfo<v8::Value>& info)
    {
      v8::Isolate* isolate = info.GetIsolate();
      v8::Local<v8::Context> context = isolate->GetCurrentContext();
      v8::Local<v8::Promise::Resolver> resolver =
          v8::Promise::Resolver::New(context).ToLocalChecked();
      info.GetReturnValue().Set(resolver->GetPromise());
    }

    void AudioContextConstructor(
        const v8::FunctionCallbackInfo<v8::Value>& info)
    {
      v8::Isolate* isolate = info.GetIsolate();
      v8::Local<v8::Context> context = isolate->GetCurrentContext();
      ContextState* state = GetState(context);
      if (!info.IsConstructCall() || state == nullptr ||
          state->audioctx_tpl.IsEmpty())
      {
        isolate->ThrowException(ToV8Str(
            isolate, "TypeError: AudioContext constructor requires new"));
        return;
      }
      double sample_rate = 0;
      if (info.Length() >= 1 && info[0]->IsObject())
      {
        v8::Local<v8::Value> raw;
        if (info[0].As<v8::Object>()
                ->Get(context, ToV8Str(isolate, "sampleRate"))
                .ToLocal(&raw) &&
            raw->IsNumber())
          sample_rate = raw->NumberValue(context).FromMaybe(0);
      }
      InitAudioContextBase(isolate, context, info.This(), sample_rate);
    }

    void OfflineAudioContextConstructor(
        const v8::FunctionCallbackInfo<v8::Value>& info)
    {
      v8::Isolate* isolate = info.GetIsolate();
      v8::Local<v8::Context> context = isolate->GetCurrentContext();
      ContextState* state = GetState(context);
      if (!info.IsConstructCall() || state == nullptr ||
          state->offlineaudioctx_tpl.IsEmpty())
      {
        isolate->ThrowException(ToV8Str(
            isolate,
            "TypeError: OfflineAudioContext constructor requires new"));
        return;
      }
      double sample_rate = 0;
      double length = 1;
      if (info.Length() >= 1 && info[0]->IsObject())
      {
        v8::Local<v8::Object> options = info[0].As<v8::Object>();
        v8::Local<v8::Value> raw;
        if (options->Get(context, ToV8Str(isolate, "sampleRate"))
                .ToLocal(&raw) &&
            raw->IsNumber())
          sample_rate = raw->NumberValue(context).FromMaybe(0);
        if (options->Get(context, ToV8Str(isolate, "length"))
                .ToLocal(&raw) &&
            raw->IsNumber())
          length = raw->NumberValue(context).FromMaybe(1);
      }
      else
      {
        length = info.Length() >= 2
            ? info[1]->NumberValue(context).FromMaybe(1)
            : 1;
        sample_rate = info.Length() >= 3
            ? info[2]->NumberValue(context).FromMaybe(48000)
            : 0;
      }
      InitAudioContextBase(isolate, context, info.This(), sample_rate);
      (void)info.This()->SetPrivate(context, AudioPrivateKey(isolate, "length"),
                                    v8::Number::New(isolate, length))
          .FromMaybe(false);
      // reference gold: no numberOfChannels own property on OfflineAudioContext
      // instances
    }

    void OfflineAudioContextStartRendering(
        const v8::FunctionCallbackInfo<v8::Value>& info)
    {
      v8::Isolate* isolate = info.GetIsolate();
      v8::Local<v8::Context> context = isolate->GetCurrentContext();
      // reference gold: the rendering promise never settles
      v8::Local<v8::Promise::Resolver> resolver =
          v8::Promise::Resolver::New(context).ToLocalChecked();
      info.GetReturnValue().Set(resolver->GetPromise());
    }

    void InstallWebAudio(v8::Isolate* isolate, v8::Local<v8::Context> context,
                         ContextState* state)
    {
      v8::Local<v8::Object> global = context->Global();
      auto register_global = [&](const char* name,
                                 v8::Local<v8::FunctionTemplate> ctor) {
        (void)global
            ->DefineOwnProperty(context, ToV8Str(isolate, name),
                                ctor->GetFunction(context).ToLocalChecked(),
                                static_cast<v8::PropertyAttribute>(v8::PropertyAttribute::DontEnum))
            .FromMaybe(false);
      };
      auto method = [&](v8::Local<v8::FunctionTemplate> target,
                        const char* name, v8::FunctionCallback cb) {
        v8::Local<v8::String> key = ToV8Str(isolate, name);
        v8::Local<v8::FunctionTemplate> fn =
            v8::FunctionTemplate::New(isolate, cb);
        fn->SetClassName(key);
        target->PrototypeTemplate()->Set(key, fn);
      };
      auto readonly = [&](v8::Local<v8::FunctionTemplate> target,
                          const char* name, v8::FunctionCallback getter_cb) {
        v8::Local<v8::FunctionTemplate> getter = v8::FunctionTemplate::New(
            isolate, getter_cb, ToV8Str(isolate, name));
        getter->SetClassName(ToV8Str(isolate, std::string("get ") + name));
        target->PrototypeTemplate()->SetAccessorProperty(
            ToV8Str(isolate, name), getter,
            v8::Local<v8::FunctionTemplate>());
      };
      auto event_slot = [&](v8::Local<v8::FunctionTemplate> target,
                            const char* name) {
        v8::Local<v8::Value> data = ToV8Str(isolate, name);
        v8::Local<v8::FunctionTemplate> getter = v8::FunctionTemplate::New(
            isolate, AudioEventSlotGetter, data);
        v8::Local<v8::FunctionTemplate> setter = v8::FunctionTemplate::New(
            isolate, AudioEventSlotSetter, data);
        getter->SetClassName(ToV8Str(isolate, std::string("get ") + name));
        setter->SetClassName(ToV8Str(isolate, std::string("set ") + name));
        target->PrototypeTemplate()->SetAccessorProperty(
            ToV8Str(isolate, name), getter, setter);
      };

      if (state->audiobuffer_tpl.IsEmpty())
      {
        v8::Local<v8::FunctionTemplate> ctor = v8::FunctionTemplate::New(
            isolate, AudioBufferConstructor);
        ctor->SetClassName(ToV8Str(isolate, "AudioBuffer"));
        v8::Local<v8::ObjectTemplate> inst = ctor->InstanceTemplate();
        inst->SetInternalFieldCount(0);
        // M10: zero own symbols on instances (tag on prototype)
        ctor->PrototypeTemplate()
            ->Set(v8::Symbol::GetToStringTag(isolate),
                  ToV8Str(isolate, "AudioBuffer"),
                  static_cast<v8::PropertyAttribute>(
                      v8::PropertyAttribute::ReadOnly |
                      v8::PropertyAttribute::DontEnum));
        method(ctor, "getChannelData", AudioBufferGetChannelData);
        // G4.285: the reference keeps the buffer's shape on the prototype
        readonly(ctor, "length", AudioLengthGetter);
        readonly(ctor, "duration", AudioNumberGetter);
        readonly(ctor, "sampleRate", AudioNumberGetter);
        readonly(ctor, "numberOfChannels", AudioNumberGetter);
        method(ctor, "copyFromChannel", AudioBufferCopyChannel);
        method(ctor, "copyToChannel", AudioBufferCopyChannel);
        ctor->SetLength(1);
        state->audiobuffer_tpl.Reset(isolate, ctor);
        register_global("AudioBuffer", ctor);
      }

      if (state->audionode_tpl.IsEmpty())
      {
        v8::Local<v8::FunctionTemplate> ctor =
            v8::FunctionTemplate::New(isolate, AudioNodeConstructor);
        ctor->SetClassName(ToV8Str(isolate, "AudioNode"));
        v8::Local<v8::ObjectTemplate> inst = ctor->InstanceTemplate();
        inst->SetInternalFieldCount(0);
        // M10: zero own symbols on instances (tag on prototype)
        ctor->PrototypeTemplate()
            ->Set(v8::Symbol::GetToStringTag(isolate),
                  ToV8Str(isolate, "AudioNode"),
                  static_cast<v8::PropertyAttribute>(
                      v8::PropertyAttribute::ReadOnly |
                      v8::PropertyAttribute::DontEnum));
        method(ctor, "connect", AudioNodeConnect);
        method(ctor, "disconnect", AudioNodeDisconnect);
        // G4.285: the reference has no start/stop here (source nodes own
        // them), but exposes context/io counts and the rw channel triple.
        readonly(ctor, "context", AudioInertObjectGetter);
        readonly(ctor, "numberOfInputs", AudioNumberGetter);
        readonly(ctor, "numberOfOutputs", AudioNumberGetter);
        for (const char* slot : {"channelCount", "channelCountMode",
                                 "channelInterpretation"})
        {
          v8::Local<v8::Value> data = ToV8Str(isolate, slot);
          v8::Local<v8::FunctionTemplate> getter = v8::FunctionTemplate::New(
              isolate,
              std::strcmp(slot, "channelCount") == 0 ? AudioNumberGetter
                                                     : AudioSlotGetter,
              data);
          v8::Local<v8::FunctionTemplate> setter =
              v8::FunctionTemplate::New(isolate, AudioEventSlotSetter, data);
          getter->SetClassName(ToV8Str(isolate, std::string("get ") + slot));
          setter->SetClassName(ToV8Str(isolate, std::string("set ") + slot));
          ctor->PrototypeTemplate()->SetAccessorProperty(
              ToV8Str(isolate, slot), getter, setter);
        }
        state->audionode_tpl.Reset(isolate, ctor);
        // reference gold: AudioNode is an exposed global (illegal constructor)
        register_global("AudioNode", ctor);
      }

      if (state->audioparam_tpl.IsEmpty())
      {
        v8::Local<v8::FunctionTemplate> ctor =
            v8::FunctionTemplate::New(isolate, AudioParamConstructor);
        ctor->SetClassName(ToV8Str(isolate, "AudioParam"));
        ctor->InstanceTemplate()->SetInternalFieldCount(0);
        state->audioparam_tpl.Reset(isolate, ctor);
        register_global("AudioParam", ctor);
      }

      if (state->baseaudioctx_tpl.IsEmpty())
      {
        v8::Local<v8::FunctionTemplate> ctor = v8::FunctionTemplate::New(
            isolate, BaseAudioContextConstructor);
        ctor->SetClassName(ToV8Str(isolate, "BaseAudioContext"));
        v8::Local<v8::ObjectTemplate> inst = ctor->InstanceTemplate();
        inst->SetInternalFieldCount(0);
        // M10: zero own symbols on instances (tag on prototype)
        ctor->PrototypeTemplate()
            ->Set(v8::Symbol::GetToStringTag(isolate),
                  ToV8Str(isolate, "BaseAudioContext"),
                  static_cast<v8::PropertyAttribute>(
                      v8::PropertyAttribute::ReadOnly |
                      v8::PropertyAttribute::DontEnum));
        // reference gold: all node factories return null
        method(ctor, "createOscillator", AudioContextCreateOscillator);
        method(ctor, "createGain", AudioContextCreateGain);
        method(ctor, "createBuffer", AudioContextCreateBuffer);
        method(ctor, "createAnalyser", AudioContextCreateNull);
        method(ctor, "createBiquadFilter", AudioContextCreateNull);
        method(ctor, "createDynamicsCompressor", AudioContextCreateNull);
        method(ctor, "createScriptProcessor", AudioContextCreateNull);
        method(ctor, "createWaveShaper", AudioContextCreateNull);
        method(ctor, "createChannelMerger", AudioContextCreateNull);
        method(ctor, "createChannelSplitter", AudioContextCreateNull);
        method(ctor, "createConvolver", AudioContextCreateNull);
        method(ctor, "createDelay", AudioContextCreateNull);
        method(ctor, "createPanner", AudioContextCreateNull);
        method(ctor, "createStereoPanner", AudioContextCreateNull);
        method(ctor, "createPeriodicWave", AudioContextCreateNull);
        method(ctor, "createConstantSource", AudioContextCreateNull);
        method(ctor, "createIIRFilter", AudioContextCreateNull);
        method(ctor, "decodeAudioData", AudioContextDecodeAudioData);
        // G4.285: resume/suspend are AudioContext-only in the reference
        method(ctor, "createBufferSource", AudioContextCreateNull);
        readonly(ctor, "destination", AudioInertObjectGetter);
        readonly(ctor, "listener", AudioInertObjectGetter);
        readonly(ctor, "audioWorklet", AudioInertObjectGetter);
        readonly(ctor, "currentTime", AudioNumberGetter);
        readonly(ctor, "sampleRate", AudioNumberGetter);
        readonly(ctor, "state", AudioSlotGetter);
        event_slot(ctor, "onstatechange");
        state->baseaudioctx_tpl.Reset(isolate, ctor);
        register_global("BaseAudioContext", ctor);
      }

      if (state->audioctx_tpl.IsEmpty())
      {
        v8::Local<v8::FunctionTemplate> ctor =
            v8::FunctionTemplate::New(isolate, AudioContextConstructor);
        ctor->SetClassName(ToV8Str(isolate, "AudioContext"));
        v8::Local<v8::ObjectTemplate> inst = ctor->InstanceTemplate();
        inst->SetInternalFieldCount(0);
        // M10: zero own symbols on instances (tag on prototype)
        ctor->PrototypeTemplate()
            ->Set(v8::Symbol::GetToStringTag(isolate),
                  ToV8Str(isolate, "AudioContext"),
                  static_cast<v8::PropertyAttribute>(
                      v8::PropertyAttribute::ReadOnly |
                      v8::PropertyAttribute::DontEnum));
        ctor->PrototypeTemplate()->Set(
            v8::Symbol::GetToStringTag(isolate),
            ToV8Str(isolate, "AudioContext"));
        ctor->Inherit(state->baseaudioctx_tpl.Get(isolate));
        // reference gold: media-source factories and lifecycle methods are
        // AudioContext-only (absent on OfflineAudioContext)
        method(ctor, "createMediaElementSource", AudioContextCreateNull);
        method(ctor, "createMediaStreamDestination", AudioContextCreateNull);
        method(ctor, "createMediaStreamSource", AudioContextCreateNull);
        method(ctor, "close", AudioContextPendingPromise);
        method(ctor, "getOutputTimestamp", AudioContextGetOutputTimestamp);
        method(ctor, "resume", AudioContextPendingPromise);
        method(ctor, "setSinkId", AudioContextPendingPromise);
        method(ctor, "suspend", AudioContextPendingPromise);
        readonly(ctor, "baseLatency", AudioFixedPropertyGetter);
        readonly(ctor, "outputLatency", AudioFixedPropertyGetter);
        readonly(ctor, "sinkId", AudioFixedPropertyGetter);
        event_slot(ctor, "onsinkchange");
        state->audioctx_tpl.Reset(isolate, ctor);
        register_global("AudioContext", ctor);
      }

      if (state->offlineaudioctx_tpl.IsEmpty())
      {
        v8::Local<v8::FunctionTemplate> ctor = v8::FunctionTemplate::New(
            isolate, OfflineAudioContextConstructor);
        ctor->SetClassName(ToV8Str(isolate, "OfflineAudioContext"));
        ctor->SetLength(1);
        v8::Local<v8::ObjectTemplate> inst = ctor->InstanceTemplate();
        inst->SetInternalFieldCount(0);
        // M10: zero own symbols on instances (tag on prototype)
        ctor->PrototypeTemplate()
            ->Set(v8::Symbol::GetToStringTag(isolate),
                  ToV8Str(isolate, "OfflineAudioContext"),
                  static_cast<v8::PropertyAttribute>(
                      v8::PropertyAttribute::ReadOnly |
                      v8::PropertyAttribute::DontEnum));
        ctor->PrototypeTemplate()->Set(
            v8::Symbol::GetToStringTag(isolate),
            ToV8Str(isolate, "OfflineAudioContext"));
        ctor->Inherit(state->baseaudioctx_tpl.Get(isolate));
        method(ctor, "startRendering", OfflineAudioContextStartRendering);
        method(ctor, "resume", AudioContextPendingPromise);
        method(ctor, "suspend", AudioContextPendingPromise);
        readonly(ctor, "length", AudioLengthGetter);
        event_slot(ctor, "oncomplete");
        state->offlineaudioctx_tpl.Reset(isolate, ctor);
        register_global("OfflineAudioContext", ctor);
      }
    }

    // ---- G4 ledger chunk 7d-1: getComputedStyle ------------------------
    // Previously the JS getComputedStyleImpl closure (polyfills4). The
    // cascade walks document.styleSheets (still a JS surface), applies
    // id/class/tag specificity and the inline style, and returns a
    // native interceptor object readable via camelCase or getPropertyValue.

    std::string KebabCase(const std::string& name)
    {
      std::string out;
      for (char c : name)
      {
        if (c >= 'A' && c <= 'Z')
        {
          out += '-';
          out += static_cast<char>(c - 'A' + 'a');
        }
        else
        {
          out += c;
        }
      }
      return out;
    }

    bool RuleMatchesSelector(v8::Isolate* isolate,
                             v8::Local<v8::Context> context,
                             const std::string& selector_text,
                             v8::Local<v8::Object> element)
    {
      // split on commas, trim each simple selector
      size_t start = 0;
      auto is_ws = [](char c) {
        return c == ' ' || c == '\t' || c == '\n' || c == '\r' ||
               c == '\f';
      };
      while (start <= selector_text.size())
      {
        size_t comma = selector_text.find(',', start);
        std::string part = selector_text.substr(
            start, comma == std::string::npos ? std::string::npos
                                              : comma - start);
        while (!part.empty() && is_ws(part.front())) part.erase(part.begin());
        while (!part.empty() && is_ws(part.back())) part.pop_back();
        if (!part.empty())
        {
          if (part == "*") return true;
          if (part[0] == '#')
          {
            v8::Local<v8::Value> id;
            if (element->Get(context, ToV8Str(isolate, "id")).ToLocal(&id) &&
                id->IsString() && ToUtf8(isolate, id) == part.substr(1))
              return true;
          }
          else if (part[0] == '.')
          {
            std::string cls = part.substr(1);
            v8::Local<v8::Value> class_attr;
            v8::Local<v8::Value> get_attr_fn;
            if (element->Get(context, ToV8Str(isolate, "getAttribute"))
                    .ToLocal(&get_attr_fn) &&
                get_attr_fn->IsFunction())
            {
              v8::Local<v8::Value> argv[] = {ToV8Str(isolate, "class")};
              v8::Local<v8::Value> raw;
              if (get_attr_fn.As<v8::Function>()
                      ->Call(context, element, 1, argv)
                      .ToLocal(&raw))
                class_attr = raw;
            }
            if (!class_attr.IsEmpty())
            {
              std::string classes = class_attr->IsString()
                  ? ToUtf8(isolate, class_attr)
                  : std::string();
              size_t pos = 0;
              while (pos <= classes.size())
              {
                size_t sp = classes.find(' ', pos);
                std::string one = classes.substr(
                    pos, sp == std::string::npos ? std::string::npos
                                                 : sp - pos);
                if (!one.empty() && one == cls) return true;
                if (sp == std::string::npos) break;
                pos = sp + 1;
              }
            }
          }
          else
          {
            v8::Local<v8::Value> tag;
            if (element->Get(context, ToV8Str(isolate, "tagName"))
                    .ToLocal(&tag) &&
                tag->IsString())
            {
              std::string tag_text = ToUtf8(isolate, tag);
              for (auto& c : tag_text)
                c = static_cast<char>(
                    std::tolower(static_cast<unsigned char>(c)));
              std::string lower_part = part;
              for (auto& c : lower_part)
                c = static_cast<char>(
                    std::tolower(static_cast<unsigned char>(c)));
              if (!tag_text.empty() && tag_text == lower_part) return true;
            }
          }
        }
        if (comma == std::string::npos) break;
        start = comma + 1;
      }
      return false;
    }

    int RuleSpecificity(const std::string& selector)
    {
      int score = 0;
      auto word_len = [&](size_t pos) {
        size_t len = 0;
        while (pos + len < selector.size())
        {
          char c = selector[pos + len];
          if (std::isalnum(static_cast<unsigned char>(c)) || c == '_' ||
              c == '-')
            len += 1;
          else
            break;
        }
        return len;
      };
      for (size_t i = 0; i < selector.size(); i++)
      {
        if (selector[i] == '#' && i + 1 < selector.size() &&
            (std::isalnum(static_cast<unsigned char>(selector[i + 1])) ||
             selector[i + 1] == '_' || selector[i + 1] == '-'))
        {
          score += 1000;
          i += word_len(i + 1);
        }
        else if (selector[i] == '.' && i + 1 < selector.size() &&
                 (std::isalnum(static_cast<unsigned char>(selector[i + 1])) ||
                  selector[i + 1] == '_' || selector[i + 1] == '-'))
        {
          score += 100;
          i += word_len(i + 1);
        }
        else if (selector[i] == '[')
        {
          // attribute selectors: skip to the closing bracket
          size_t close = selector.find(']', i);
          if (close == std::string::npos) break;
          i = close;
        }
        else
        {
          char c = selector[i];
          bool boundary = i == 0 || c == ' ' || c == '>' || c == '+' ||
                          c == '~' || c == '(';
          char next = i + 1 < selector.size() ? selector[i + 1] : '\0';
          if (boundary && std::isalpha(static_cast<unsigned char>(next)))
          {
            score += 1;
            i += word_len(i + 1);
          }
        }
      }
      return score;
    }

    void ParseDeclarations(const std::string& body,
                           std::vector<std::pair<std::string, std::string>>& out)
    {
      size_t start = 0;
      while (start <= body.size())
      {
        size_t semi = body.find(';', start);
        std::string item = body.substr(
            start, semi == std::string::npos ? std::string::npos
                                             : semi - start);
        size_t colon = item.find(':');
        if (colon != std::string::npos)
        {
          std::string name = item.substr(0, colon);
          std::string value = item.substr(colon + 1);
          auto is_ws = [](char c) {
            return c == ' ' || c == '\t' || c == '\n' || c == '\r' ||
                   c == '\f';
          };
          while (!name.empty() && is_ws(name.front())) name.erase(name.begin());
          while (!name.empty() && is_ws(name.back())) name.pop_back();
          while (!value.empty() && is_ws(value.front()))
            value.erase(value.begin());
          while (!value.empty() && is_ws(value.back())) value.pop_back();
          for (auto& c : name)
            c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
          if (!name.empty() && !value.empty())
            out.emplace_back(name, value);
        }
        if (semi == std::string::npos) break;
        start = semi + 1;
      }
    }

    v8::Intercepted GcsPropertyGetter(v8::Local<v8::Name> property, const v8::PropertyCallbackInfo<v8::Value>& info)
    {
            v8::Isolate* iso = info.GetIsolate();
            v8::Local<v8::Context> ctx = iso->GetCurrentContext();
            v8::Local<v8::Value> name = property;
            if (!name.IsEmpty() && !name->IsString())
            {
              // symbols (Symbol.toStringTag et al) must fall through so
              // Object.prototype.toString sees the real tag
              return v8::Intercepted::kNo;
            }
            if (!name.IsEmpty() && name->IsString())
            {
              std::string prop = ToUtf8(iso, name);
              // Object.prototype conversion methods must fall through:
              // intercepting them makes ToPrimitive throw
              // "Cannot convert object to primitive value" (the challenge
              // VM's CSS sweep trips exactly that)
              static const char* const reserved[] = {
                  "toString", "valueOf", "toLocaleString", "constructor",
                  "hasOwnProperty", "isPrototypeOf",
                  "propertyIsEnumerable", "toLocaleString"};
              for (const char* reserved_name : reserved)
              {
                if (prop == reserved_name) return v8::Intercepted::kNo;
              }
              static const char* const known[] = {
                  "getPropertyValue", "item", "length", "cssText"};
              for (const char* known_name : known)
              {
                if (prop == known_name)
                {
                  v8::Local<v8::Value> api;
                  if (info.This()
                          ->GetPrivate(
                              ctx,
                              v8::Private::ForApi(
                                  iso, ToV8Str(iso, "lv8.gcs.api")))
                          .ToLocal(&api) &&
                      api->IsObject())
                  {
                    v8::Local<v8::Value> value;
                    if (api.As<v8::Object>()
                            ->Get(ctx, ToV8Str(iso, prop))
                            .ToLocal(&value))
                    {
                      info.GetReturnValue().Set(value);
                      return v8::Intercepted::kYes;
                    }
                  }
                  return v8::Intercepted::kNo;
                }
              }
              bool camel_ok = !prop.empty() &&
                              std::isalpha(static_cast<unsigned char>(prop[0]));
              for (size_t i = 1; i < prop.size() && camel_ok; i++)
                camel_ok = std::isalpha(static_cast<unsigned char>(prop[i])) ||
                           std::isdigit(static_cast<unsigned char>(prop[i]));
              if (camel_ok)
              {
                // reference parity: computed values read as empty strings no
                // matter the cascade (measured on the reference wheel)
                info.GetReturnValue().Set(ToV8Str(iso, ""));
                return v8::Intercepted::kYes;
              }
              return v8::Intercepted::kNo;
            }
              }

    void InstallComputedStyle(v8::Isolate* isolate,
                              v8::Local<v8::Context> context,
                              ContextState* state)
    {
      if (!state->gcs_tpl.IsEmpty()) return;
      v8::Local<v8::FunctionTemplate> ctor = v8::FunctionTemplate::New(
          isolate,
          [](const v8::FunctionCallbackInfo<v8::Value>& info) {
            v8::Isolate* iso = info.GetIsolate();
            v8::Local<v8::Context> ctx = iso->GetCurrentContext();
            ContextState* st = GetState(ctx);
            if (st == nullptr || st->gcs_tpl.IsEmpty() || info.Length() < 1 ||
                !info[0]->IsObject())
            {
              iso->ThrowException(ToV8Str(
                  iso, "TypeError: getComputedStyle requires an element"));
              return;
            }
            v8::Local<v8::Object> element = info[0].As<v8::Object>();

            // cascade: document.styleSheets rules, then inline style
            std::vector<std::pair<std::string,
                                  std::pair<std::string, int>>>
                resolved;
            auto apply = [&](const std::vector<
                                 std::pair<std::string, std::string>>& decls,
                             int specificity) {
              for (const auto& decl : decls)
              {
                bool replaced = false;
                for (auto& row : resolved)
                {
                  if (row.first == decl.first)
                  {
                    if (specificity >= row.second.second)
                      row.second = {decl.second, specificity};
                    replaced = true;
                    break;
                  }
                }
                if (!replaced)
                  resolved.emplace_back(
                      decl.first,
                      std::make_pair(decl.second, specificity));
              }
            };
            auto get_text = [&](v8::Local<v8::Object> obj,
                                const char* key) {
              v8::Local<v8::Value> value;
              return obj->Get(ctx, ToV8Str(iso, key)).ToLocal(&value)
                  ? ToUtf8(iso, value)
                  : std::string();
            };

            v8::Local<v8::Value> document;
            if (ctx->Global()
                    ->Get(ctx, ToV8Str(iso, "document"))
                    .ToLocal(&document) &&
                document->IsObject())
            {
              v8::Local<v8::Value> sheets;
              if (document.As<v8::Object>()
                      ->Get(ctx, ToV8Str(iso, "styleSheets"))
                      .ToLocal(&sheets) &&
                  sheets->IsObject())
              {
                // sheets may be an array or array-like with iteration
                v8::Local<v8::Value> sheets_value = sheets;
                v8::Local<v8::Array> sheet_list;
                if (sheets_value->IsArray())
                {
                  sheet_list = sheets_value.As<v8::Array>();
                }
                else
                {
                  sheet_list = v8::Array::New(iso);
                  // array-like: walk numeric indices until a miss
                  for (uint32_t i = 0;; i++)
                  {
                    v8::Local<v8::Value> sheet;
                    if (!sheets_value
                             .As<v8::Object>()
                             ->Get(ctx, i)
                             .ToLocal(&sheet) ||
                        sheet->IsUndefined())
                      break;
                    (void)sheet_list->Set(ctx, i, sheet).FromMaybe(false);
                  }
                }
                for (uint32_t s = 0; s < sheet_list->Length(); s++)
                {
                  v8::Local<v8::Value> sheet;
                  if (!sheet_list->Get(ctx, s).ToLocal(&sheet) ||
                      !sheet->IsObject())
                    continue;
                  v8::Local<v8::Value> rules;
                  if (!sheet.As<v8::Object>()
                            ->Get(ctx, ToV8Str(iso, "cssRules"))
                            .ToLocal(&rules) ||
                      !rules->IsArray())
                    continue;
                  v8::Local<v8::Array> rule_list = rules.As<v8::Array>();
                  for (uint32_t r = 0; r < rule_list->Length(); r++)
                  {
                    v8::Local<v8::Value> rule;
                    if (!rule_list->Get(ctx, r).ToLocal(&rule) ||
                        !rule->IsObject())
                      continue;
                    std::string selector =
                        get_text(rule.As<v8::Object>(), "selectorText");
                    if (!RuleMatchesSelector(iso, ctx, selector, element))
                      continue;
                    std::string body = get_text(rule.As<v8::Object>(), "_body");
                    if (body.empty())
                      body = get_text(rule.As<v8::Object>(), "cssText");
                    std::vector<std::pair<std::string, std::string>> decls;
                    ParseDeclarations(body, decls);
                    apply(decls, RuleSpecificity(selector));
                  }
                }
              }
            }
            // inline style wins (specificity 100000)
            v8::Local<v8::Value> get_attr;
            if (element->Get(ctx, ToV8Str(iso, "getAttribute"))
                    .ToLocal(&get_attr) &&
                get_attr->IsFunction())
            {
              v8::Local<v8::Value> argv[] = {ToV8Str(iso, "style")};
              v8::Local<v8::Value> inline_style;
              if (get_attr.As<v8::Function>()
                      ->Call(ctx, element, 1, argv)
                      .ToLocal(&inline_style) &&
                  inline_style->IsString())
              {
                std::string inline_text = ToUtf8(iso, inline_style);
                if (!inline_text.empty())
                {
                  std::vector<std::pair<std::string, std::string>> decls;
                  ParseDeclarations(inline_text, decls);
                  apply(decls, 100000);
                }
              }
            }

            // api object: getPropertyValue/item/length/cssText
            v8::Local<v8::Object> api = v8::Object::New(iso);
            // getPropertyValue needs the resolved map; store the resolved
            // pairs on the api object as plain props (name -> value)
            for (const auto& row : resolved)
              (void)api->Set(ctx, ToV8Str(iso, row.first),
                             ToV8Str(iso, row.second.first))
                  .FromMaybe(false);
            v8::Local<v8::Function> get_property_value =
                v8::FunctionTemplate::New(
                    iso,
                    [](const v8::FunctionCallbackInfo<v8::Value>& args) {
                      v8::Isolate* iso2 = args.GetIsolate();
                      v8::Local<v8::Context> ctx2 = iso2->GetCurrentContext();
                      std::string wanted = args.Length() >= 1 &&
                                                   args[0]->IsString()
                                               ? ToUtf8(iso2, args[0])
                                               : "";
                      for (auto& c : wanted)
                        c = static_cast<char>(std::tolower(
                            static_cast<unsigned char>(c)));
                      (void)wanted;
                      // reference parity: computed reads are empty
                      args.GetReturnValue().Set(ToV8Str(iso2, ""));
                    })
                    ->GetFunction(ctx)
                    .ToLocalChecked();
            (void)api->Set(ctx, ToV8Str(iso, "getPropertyValue"),
                           get_property_value)
                .FromMaybe(false);
            (void)api->Set(ctx, ToV8Str(iso, "length"),
                           v8::Number::New(iso, 377.0))
                .FromMaybe(false);
            (void)api->Set(ctx, ToV8Str(iso, "cssText"), ToV8Str(iso, ""))
                .FromMaybe(false);
            v8::Local<v8::FunctionTemplate> item_fn =
                v8::FunctionTemplate::New(
                    iso, [](const v8::FunctionCallbackInfo<v8::Value>& args) {
                      v8::Isolate *iso2 = args.GetIsolate();
                      int idx = args.Length() >= 1 && args[0]->IsInt32()
                                    ? args[0].As<v8::Int32>()->Value()
                                    : -1;
                      if (idx >= 0 && idx < kCssPropertyCount)
                        args.GetReturnValue().Set(
                            ToV8Str(iso2, kCssPropertyNames[idx]));
                      else
                        args.GetReturnValue().Set(ToV8Str(iso2, ""));
                    });
            item_fn->SetClassName(ToV8Str(iso, "item"));
            (void)api->Set(ctx, ToV8Str(iso, "item"),
                           item_fn->GetFunction(ctx).ToLocalChecked())
                .FromMaybe(false);

            v8::Local<v8::Object> view = st->gcs_tpl.Get(iso)
                                             ->InstanceTemplate()
                                             ->NewInstance(ctx)
                                             .ToLocalChecked();
            (void)view->SetPrivate(
                ctx,
                v8::Private::ForApi(iso, ToV8Str(iso, "lv8.gcs.api")), api)
                .FromMaybe(false);
            // M6: zero own symbols - the gcs template prototype carries the
            // tag (installed where gcs_tpl is registered)
            // reference parity: the computed style enumerates the full Chrome
            // property-name list via numeric indices, plus every camelCase
            // property name as an own property (1006 own names total)
            for (int idx = 0; idx < kCssPropertyCount; idx++)
              (void)view->Set(ctx, v8::Integer::New(iso, idx),
                              ToV8Str(iso, kCssPropertyNames[idx]))
                  .FromMaybe(false);
            for (int ci = 0; ci < kCssComputedCamelCount; ci++)
              (void)view->Set(ctx, ToV8Str(iso, kCssComputedCamelNames[ci]),
                              ToV8Str(iso, ""))
                  .FromMaybe(false);
            info.GetReturnValue().Set(view);
          });
      ctor->SetClassName(ToV8Str(isolate, "getComputedStyle"));
      v8::Local<v8::ObjectTemplate> inst = ctor->InstanceTemplate();
      inst->SetInternalFieldCount(0);
      v8::NamedPropertyHandlerConfiguration handler_config(GcsPropertyGetter);
      inst->SetHandler(handler_config);
      // M6: the view prototype carries the toString tag (instances keep
      // zero own symbols)
      ctor->PrototypeTemplate()
          ->Set(v8::Symbol::GetToStringTag(isolate),
                ToV8Str(isolate, "CSSStyleDeclaration"),
                static_cast<v8::PropertyAttribute>(
                    v8::PropertyAttribute::ReadOnly |
                    v8::PropertyAttribute::DontEnum));
      state->gcs_tpl.Reset(isolate, ctor);
      (void)context
          ->Global()
          ->DefineOwnProperty(context, ToV8Str(isolate, "getComputedStyle"),
                              ctor->GetFunction(context).ToLocalChecked(),
                              static_cast<v8::PropertyAttribute>(0))
          .FromMaybe(false);
    }

    // ---- G4 ledger chunk 7d-2: MutationObserver ------------------------
    // The observer class, registry and delivery engine are native; the
    // JS element-patch wrappers (setAttribute/appendChild/...) call the
    // native __lv8_notifyMutation(target, record) and the wrapping helper
    // (__lv8_wrapMutationSubtree) stays on the JS registration surface.

    v8::Local<v8::Private> MoObserversKey(v8::Isolate* isolate)
    {
      return v8::Private::ForApi(isolate,
                                 ToV8Str(isolate, "lv8.mutation.observers"));
    }

    v8::Local<v8::Array> MoObservers(v8::Isolate* isolate,
                                     v8::Local<v8::Context> context)
    {
      v8::Local<v8::Value> observers;
      if (!context->Global()
                ->GetPrivate(context, MoObserversKey(isolate))
                .ToLocal(&observers) ||
          !observers->IsArray())
      {
        v8::Local<v8::Array> created = v8::Array::New(isolate);
        (void)context->Global()
            ->SetPrivate(context, MoObserversKey(isolate), created)
            .FromMaybe(false);
        return created;
      }
      return observers.As<v8::Array>();
    }

    void MoThrowTypeError(v8::Isolate* isolate,
                          v8::Local<v8::Context> context,
                          const std::string& message)
    {
      v8::Local<v8::Value> ctor;
      if (context->Global()
              ->Get(context, ToV8Str(isolate, "TypeError"))
              .ToLocal(&ctor) &&
          ctor->IsFunction())
      {
        v8::Local<v8::Value> argv[] = {ToV8Str(isolate, message)};
        v8::Local<v8::Value> error;
        if (ctor.As<v8::Function>()
                ->NewInstance(context, 1, argv)
                .ToLocal(&error) &&
            !error.IsEmpty())
        {
          isolate->ThrowException(error);
          return;
        }
      }
      isolate->ThrowException(ToV8Str(isolate, "TypeError: " + message));
    }

    v8::Local<v8::Object> MakeMutationObserver(
        v8::Isolate* isolate, v8::Local<v8::Context> context,
        ContextState* state, v8::Local<v8::Value> callback)
    {
      v8::Local<v8::Object> inst = state->mutationobserver_tpl.Get(isolate)
                                       ->InstanceTemplate()
                                       ->NewInstance(context)
                                       .ToLocalChecked();
      (void)inst->Set(context, ToV8Str(isolate, "_callback"), callback)
          .FromMaybe(false);
      (void)inst->Set(context, ToV8Str(isolate, "_observing"),
                      v8::Array::New(isolate))
          .FromMaybe(false);
      v8::Local<v8::Array> observers = MoObservers(isolate, context);
      (void)observers->Set(context, observers->Length(), inst)
          .FromMaybe(false);
      return inst;
    }

    void MutationObserverConstructor(
        const v8::FunctionCallbackInfo<v8::Value>& info)
    {
      v8::Isolate* isolate = info.GetIsolate();
      v8::Local<v8::Context> context = isolate->GetCurrentContext();
      ContextState* state = GetState(context);
      if (!info.IsConstructCall() || state == nullptr ||
          state->mutationobserver_tpl.IsEmpty())
      {
        isolate->ThrowException(ToV8Str(
            isolate, "TypeError: MutationObserver constructor requires new"));
        return;
      }
      if (info.Length() < 1)
      {
        // M12-argreq: reference webidl 0-arg message (full text measured in
        // work/reference-traces/_m14_errshapes.json)
        isolate->ThrowException(v8::Exception::TypeError(ToV8Str(
            isolate,
            "Failed to construct 'MutationObserver': 1 argument required, "
            "but only 0 present.")));
        return;
      }
      if (!info[0]->IsFunction())
      {
        MoThrowTypeError(isolate, context, "callback must be a function");
        return;
      }
      info.GetReturnValue().Set(MakeMutationObserver(
          isolate, context, state, info[0]));
    }

    void MutationObserverObserve(
        const v8::FunctionCallbackInfo<v8::Value>& info)
    {
      v8::Isolate* isolate = info.GetIsolate();
      v8::Local<v8::Context> context = isolate->GetCurrentContext();
      v8::Local<v8::Value> observing;
      if (!info.This()
               ->Get(context, ToV8Str(isolate, "_observing"))
               .ToLocal(&observing) ||
          !observing->IsArray())
        return;
      v8::Local<v8::Object> entry = v8::Object::New(isolate);
      v8::Local<v8::Value> target =
          info.Length() >= 1
              ? info[0]
              : v8::Local<v8::Value>(v8::Undefined(isolate));
      v8::Local<v8::Value> options =
          info.Length() >= 2 && info[1]->IsObject()
              ? info[1]
              : v8::Local<v8::Value>(v8::Object::New(isolate));
      (void)entry->Set(context, ToV8Str(isolate, "target"), target)
          .FromMaybe(false);
      (void)entry->Set(context, ToV8Str(isolate, "options"), options)
          .FromMaybe(false);
      (void)observing.As<v8::Array>()
          ->Set(context, observing.As<v8::Array>()->Length(), entry)
          .FromMaybe(false);
      v8::Local<v8::Value> wrap;
      wrap = InternalBridgeGet(isolate, context, "wrapMutationSubtree");
      if (wrap->IsFunction())
      {
        v8::Local<v8::Value> argv[] = {target};
        v8::TryCatch try_catch(isolate);
        (void)wrap.As<v8::Function>()
            ->Call(context, context->Global(), 1, argv)
            .FromMaybe(v8::Local<v8::Value>());
        try_catch.Reset();
      }
    }

    void MutationObserverDisconnect(
        const v8::FunctionCallbackInfo<v8::Value>& info)
    {
      v8::Isolate* isolate = info.GetIsolate();
      v8::Local<v8::Context> context = isolate->GetCurrentContext();
      (void)info.This()->Set(context,
                             ToV8Str(isolate, "_observing"),
                             v8::Array::New(isolate))
          .FromMaybe(false);
    }

    void MutationObserverTakeRecords(
        const v8::FunctionCallbackInfo<v8::Value>& info)
    {
      info.GetReturnValue().Set(
          v8::Array::New(info.GetIsolate()));
    }

    // B1: browser-true MutationObserver delivery -------------------------
    //
    // Browsers queue every matching record on the observer and deliver them
    // ALL in ONE callback(records, observer) microtask, coalescing records
    // that share type+target (appending their addedNodes/removedNodes).
    // lv8 used to invoke the callback once per record with a single-entry
    // array, so the WAF signer walked a different record count than under
    // reference/Chrome and diverged into an undefined-register branch.

    void MoDeliverRunner(const v8::FunctionCallbackInfo<v8::Value>& info)
    {
      v8::Isolate* isolate = info.GetIsolate();
      v8::Local<v8::Context> context = isolate->GetCurrentContext();
      v8::Local<v8::Array> data = info.Data().As<v8::Array>();
      v8::Local<v8::Value> observer;
      if (!data->Get(context, 0).ToLocal(&observer) || !observer->IsObject())
        return;
      v8::Local<v8::Object> obs = observer.As<v8::Object>();
      v8::Local<v8::Value> pending;
      if (!obs->Get(context, ToV8Str(isolate, "_pending")).ToLocal(&pending) ||
          !pending->IsArray())
        return;
      v8::Local<v8::Array> records = pending.As<v8::Array>();
      if (records->Length() == 0)
        return;
      v8::Local<v8::Array> fresh = v8::Array::New(isolate);
      (void)obs->Set(context, ToV8Str(isolate, "_pending"), fresh)
          .FromMaybe(false);
      (void)obs->Set(context, ToV8Str(isolate, "_scheduled"),
                     v8::Boolean::New(isolate, false))
          .FromMaybe(false);
      v8::Local<v8::Value> callback;
      if (!obs->Get(context, ToV8Str(isolate, "_callback")).ToLocal(&callback) ||
          !callback->IsFunction())
        return;
      v8::Local<v8::Value> argv[] = {records, observer};
      v8::TryCatch try_catch(isolate);
      (void)callback.As<v8::Function>()
          ->Call(context, observer, 2, argv)
          .FromMaybe(v8::Local<v8::Value>());
      try_catch.Reset();
    }

    void MoSchedule(v8::Isolate* isolate, v8::Local<v8::Context> context,
                    v8::Local<v8::Object> observer)
    {
      v8::Local<v8::Value> scheduled;
      if (observer->Get(context, ToV8Str(isolate, "_scheduled"))
              .ToLocal(&scheduled) &&
          !scheduled.IsEmpty() && scheduled->BooleanValue(isolate))
        return;
      (void)observer->Set(context, ToV8Str(isolate, "_scheduled"),
                          v8::Boolean::New(isolate, true))
          .FromMaybe(false);
      v8::Local<v8::Array> payload = v8::Array::New(isolate, 1);
      (void)payload->Set(context, 0, observer).FromMaybe(false);
      v8::Local<v8::FunctionTemplate> runner =
          v8::FunctionTemplate::New(isolate, MoDeliverRunner, payload);
      runner->SetClassName(ToV8Str(isolate, ""));
      v8::Local<v8::Value> queue;
      if (context->Global()
              ->Get(context, ToV8Str(isolate, "queueMicrotask"))
              .ToLocal(&queue) &&
          queue->IsFunction())
      {
        v8::Local<v8::Value> argv[] = {
            runner->GetFunction(context).ToLocalChecked()};
        v8::TryCatch try_catch(isolate);
        (void)queue.As<v8::Function>()
            ->Call(context, context->Global(), 1, argv)
            .FromMaybe(v8::Local<v8::Value>());
        try_catch.Reset();
      }
    }

    void MoEnqueue(v8::Isolate* isolate, v8::Local<v8::Context> context,
                   v8::Local<v8::Object> observer,
                   v8::Local<v8::Object> record)
    {
      v8::Local<v8::Value> pending;
      if (!observer->Get(context, ToV8Str(isolate, "_pending"))
               .ToLocal(&pending) ||
          !pending->IsArray())
      {
        pending = v8::Array::New(isolate);
        (void)observer->Set(context, ToV8Str(isolate, "_pending"), pending)
            .FromMaybe(false);
      }
      v8::Local<v8::Array> queue = pending.As<v8::Array>();
      uint32_t length = queue->Length();
      if (length > 0)
      {
        // spec coalescing: same type+target merges node lists
        v8::Local<v8::Value> last;
        if (queue->Get(context, length - 1).ToLocal(&last) &&
            last->IsObject())
        {
          v8::Local<v8::Value> lt, rt, lg, rg;
          bool same = last.As<v8::Object>()
                              ->Get(context, ToV8Str(isolate, "type"))
                              .ToLocal(&lt) &&
                      record->Get(context, ToV8Str(isolate, "type"))
                          .ToLocal(&rt) &&
                      lt->StrictEquals(rt) &&
                      last.As<v8::Object>()
                              ->Get(context, ToV8Str(isolate, "target"))
                              .ToLocal(&lg) &&
                      record->Get(context, ToV8Str(isolate, "target"))
                          .ToLocal(&rg) &&
                      lg->SameValue(rg);
          if (same)
          {
            for (const char* key : {"addedNodes", "removedNodes"})
            {
              v8::Local<v8::Value> dst, src;
              if (last.As<v8::Object>()
                      ->Get(context, ToV8Str(isolate, key))
                      .ToLocal(&dst) &&
                  dst->IsArray() &&
                  record->Get(context, ToV8Str(isolate, key)).ToLocal(&src) &&
                  src->IsArray())
              {
                v8::Local<v8::Array> d = dst.As<v8::Array>();
                v8::Local<v8::Array> s = src.As<v8::Array>();
                for (uint32_t k = 0; k < s->Length(); k++)
                {
                  v8::Local<v8::Value> item;
                  if (s->Get(context, k).ToLocal(&item))
                    (void)d->Set(context, d->Length(), item).FromMaybe(false);
                }
              }
            }
            MoSchedule(isolate, context, observer);
            return;
          }
        }
      }
      (void)queue->Set(context, queue->Length(), record).FromMaybe(false);
      MoSchedule(isolate, context, observer);
    }

    void MoNotifyRunner(const v8::FunctionCallbackInfo<v8::Value>& info)
    {
      v8::Isolate* isolate = info.GetIsolate();
      v8::Local<v8::Context> context = isolate->GetCurrentContext();
      v8::Local<v8::Array> data = info.Data().As<v8::Array>();
      v8::Local<v8::Value> target, record;
      if (!data->Get(context, 0).ToLocal(&target) ||
          !data->Get(context, 1).ToLocal(&record))
        return;
      v8::Local<v8::Array> observers = MoObservers(isolate, context);
      for (uint32_t i = 0; i < observers->Length(); i++)
      {
        v8::Local<v8::Value> observer;
        if (!observers->Get(context, i).ToLocal(&observer) ||
            !observer->IsObject())
          continue;
        v8::Local<v8::Value> observing;
        if (!observer.As<v8::Object>()
                  ->Get(context, ToV8Str(isolate, "_observing"))
                  .ToLocal(&observing) ||
            !observing->IsArray())
          continue;
        v8::Local<v8::Array> entries = observing.As<v8::Array>();
        for (uint32_t e = 0; e < entries->Length(); e++)
        {
          v8::Local<v8::Value> entry;
          if (!entries->Get(context, e).ToLocal(&entry) || !entry->IsObject())
            continue;
          v8::Local<v8::Value> entry_target;
          if (!entry.As<v8::Object>()
                   ->Get(context, ToV8Str(isolate, "target"))
                   .ToLocal(&entry_target))
            continue;
          // scope walk: target up through parentNode/parentElement
          v8::Local<v8::Value> node = target;
          bool in_scope = false;
          while (!node.IsEmpty() && node->IsObject())
          {
            if (node->SameValue(entry_target))
            {
              in_scope = true;
              break;
            }
            v8::Local<v8::Value> parent;
            if (!node.As<v8::Object>()
                     ->Get(context, ToV8Str(isolate, "parentNode"))
                     .ToLocal(&parent) ||
                parent.IsEmpty() || parent->IsNull() || parent->IsUndefined())
            {
              if (!node.As<v8::Object>()
                       ->Get(context, ToV8Str(isolate, "parentElement"))
                       .ToLocal(&parent))
                break;
            }
            if (parent.IsEmpty() || parent->IsNull() || parent->IsUndefined())
              break;
            node = parent;
          }
          if (!in_scope) continue;
          v8::Local<v8::Value> record_type;
          if (record.IsEmpty() || !record->IsObject() ||
              !record.As<v8::Object>()
                   ->Get(context, ToV8Str(isolate, "type"))
                   .ToLocal(&record_type) ||
              !record_type->IsString())
            continue;
          std::string type = ToUtf8(isolate, record_type);
          v8::Local<v8::Value> options;
          if (!entry.As<v8::Object>()
                   ->Get(context, ToV8Str(isolate, "options"))
                   .ToLocal(&options) ||
              !options->IsObject())
            options = v8::Object::New(isolate);
          v8::Local<v8::Value> opt_flag;
          std::string opt_key = type == "childList"
              ? "childList"
              : (type == "characterData" ? "characterData" : "attributes");
          if (!options.As<v8::Object>()
                    ->Get(context, ToV8Str(isolate, opt_key))
                    .ToLocal(&opt_flag) ||
              opt_flag.IsEmpty() || !opt_flag->BooleanValue(isolate))
            continue;
          // queue + coalesce; one microtask delivers all records together
          MoEnqueue(isolate, context, observer.As<v8::Object>(),
                    record.As<v8::Object>());
        }
      }
    }

    void MoNotify(const v8::FunctionCallbackInfo<v8::Value>& info)
    {
      v8::Isolate* isolate = info.GetIsolate();
      v8::Local<v8::Context> context = isolate->GetCurrentContext();
      if (info.Length() < 2) return;
      v8::Local<v8::Array> payload = v8::Array::New(isolate, 2);
      (void)payload->Set(context, 0, info[0]).FromMaybe(false);
      (void)payload->Set(context, 1, info[1]).FromMaybe(false);
      v8::Local<v8::FunctionTemplate> runner =
          v8::FunctionTemplate::New(isolate, MoNotifyRunner, payload);
      runner->SetClassName(ToV8Str(isolate, ""));
      v8::Local<v8::Value> queue;
      if (context->Global()
              ->Get(context, ToV8Str(isolate, "queueMicrotask"))
              .ToLocal(&queue) &&
          queue->IsFunction())
      {
        v8::Local<v8::Value> argv[] = {
            runner->GetFunction(context).ToLocalChecked()};
        v8::TryCatch try_catch(isolate);
        (void)queue.As<v8::Function>()
            ->Call(context, context->Global(), 1, argv)
            .FromMaybe(v8::Local<v8::Value>());
        try_catch.Reset();
      }
    }

    // ---- G4.19: Resize/Intersection observers --------------------------
    // target.style.getPropertyValue(prop) parsed like JS parseFloat
    double StyleNumber(v8::Isolate* isolate, v8::Local<v8::Context> context,
                       v8::Local<v8::Object> target, const char* prop)
    {
      v8::Local<v8::Value> style;
      if (!target->Get(context, ToV8Str(isolate, "style")).ToLocal(&style) ||
          !style->IsObject())
        return 0.0;
      v8::Local<v8::Value> get_fn;
      if (!style.As<v8::Object>()
               ->Get(context, ToV8Str(isolate, "getPropertyValue"))
               .ToLocal(&get_fn) ||
          !get_fn->IsFunction())
        return 0.0;
      v8::Local<v8::Value> argv[1] = {ToV8Str(isolate, prop)};
      v8::Local<v8::Value> raw;
      v8::TryCatch try_catch(isolate);
      if (!get_fn.As<v8::Function>()
               ->Call(context, style, 1, argv)
               .ToLocal(&raw) ||
          raw.IsEmpty() || !raw->IsString())
      {
        try_catch.Reset();
        return 0.0;
      }
      try_catch.Reset();
      try
      {
        return std::stod(ToUtf8(isolate, raw));
      }
      catch (const std::exception&)
      {
        return 0.0;
      }
    }

    v8::Local<v8::Value> MakeDomRect(v8::Isolate* isolate,
                                  v8::Local<v8::Context> context, double x,
                                  double y, double w, double h)
    {
      v8::Local<v8::Value> ctor;
      if (!context->Global()
                   ->Get(context, ToV8Str(isolate, "DOMRect"))
                   .ToLocal(&ctor) ||
          !ctor->IsFunction())
        return v8::Undefined(isolate);
      v8::Local<v8::Value> argv[4] = {
          v8::Number::New(isolate, x), v8::Number::New(isolate, y),
          v8::Number::New(isolate, w), v8::Number::New(isolate, h)};
      v8::Local<v8::Value> rect;
      if (ctor.As<v8::Function>()
              ->NewInstance(context, 4, argv)
              .ToLocal(&rect))
        return rect;
      return v8::Undefined(isolate);
    }

    // queue a microtask: callback([entry], observer)
    void QueueObserverCallback(
        v8::Isolate* isolate, v8::Local<v8::Context> context,
        const ContextState::DomObserver& observer,
        v8::Local<v8::Object> entry)
    {
      v8::Local<v8::Array> records = v8::Array::New(isolate, 1);
      (void)records->Set(context, 0, entry).FromMaybe(false);
      v8::Local<v8::Array> payload = v8::Array::New(isolate, 3);
      (void)payload->Set(context, 0, observer.callback.Get(isolate))
          .FromMaybe(false);
      (void)payload->Set(context, 1, observer.instance.Get(isolate))
          .FromMaybe(false);
      (void)payload->Set(context, 2, records).FromMaybe(false);
      v8::Local<v8::FunctionTemplate> runner = v8::FunctionTemplate::New(
          isolate,
          [](const v8::FunctionCallbackInfo<v8::Value>& info) {
            v8::Isolate* iso = info.GetIsolate();
            v8::Local<v8::Context> ctx = iso->GetCurrentContext();
            v8::Local<v8::Array> data = info.Data().As<v8::Array>();
            v8::Local<v8::Value> cb, recv, records;
            if (!data->Get(ctx, 0).ToLocal(&cb) ||
                !data->Get(ctx, 1).ToLocal(&recv) ||
                !data->Get(ctx, 2).ToLocal(&records) ||
                !cb->IsFunction())
              return;
            v8::Local<v8::Value> argv[2] = {records, recv};
            v8::TryCatch try_catch(iso);
            (void)cb.As<v8::Function>()
                ->Call(ctx, recv, 2, argv)
                .FromMaybe(v8::Local<v8::Value>());
            try_catch.Reset();
          },
          payload);
      runner->SetClassName(ToV8Str(isolate, ""));
      v8::Local<v8::Value> queue;
      if (context->Global()
              ->Get(context, ToV8Str(isolate, "queueMicrotask"))
              .ToLocal(&queue) &&
          queue->IsFunction())
      {
        v8::Local<v8::Value> argv[] = {
            runner->GetFunction(context).ToLocalChecked()};
        v8::TryCatch try_catch(isolate);
        (void)queue.As<v8::Function>()
            ->Call(context, context->Global(), 1, argv)
            .FromMaybe(v8::Local<v8::Value>());
        try_catch.Reset();
      }
    }

    void FireResize(v8::Isolate* isolate, v8::Local<v8::Context> context,
                    const ContextState::DomObserver& observer,
                    v8::Local<v8::Object> target)
    {
      double w = StyleNumber(isolate, context, target, "width");
      double h = StyleNumber(isolate, context, target, "height");
      v8::Local<v8::Object> entry = v8::Object::New(isolate);
      (void)entry->Set(context, ToV8Str(isolate, "target"), target)
          .FromMaybe(false);
      (void)entry
          ->Set(context, ToV8Str(isolate, "contentRect"),
                MakeDomRect(isolate, context, 0, 0, w, h))
          .FromMaybe(false);
      v8::Local<v8::Object> size = v8::Object::New(isolate);
      (void)size
          ->Set(context, ToV8Str(isolate, "inlineSize"),
                v8::Number::New(isolate, w))
          .FromMaybe(false);
      (void)size
          ->Set(context, ToV8Str(isolate, "blockSize"),
                v8::Number::New(isolate, h))
          .FromMaybe(false);
      v8::Local<v8::Array> boxes = v8::Array::New(isolate, 1);
      (void)boxes->Set(context, 0, size).FromMaybe(false);
      (void)entry->Set(context, ToV8Str(isolate, "borderBoxSize"), boxes)
          .FromMaybe(false);
      (void)entry->Set(context, ToV8Str(isolate, "contentBoxSize"), boxes)
          .FromMaybe(false);
      QueueObserverCallback(isolate, context, observer, entry);
    }

    void FireIntersection(v8::Isolate* isolate,
                          v8::Local<v8::Context> context,
                          ContextState* state,
                          const ContextState::DomObserver& observer,
                          v8::Local<v8::Object> target)
    {
      double root_w = 1024.0;
      double root_h = 768.0;
      if (state != nullptr && !state->profile_data.IsEmpty())
      {
        v8::Local<v8::Value> screen;
        if (state->profile_data.Get(isolate)
                ->Get(context, ToV8Str(isolate, "screen"))
                .ToLocal(&screen) &&
            screen->IsObject())
        {
          v8::Local<v8::Value> sw, sh;
          if (screen.As<v8::Object>()
                  ->Get(context, ToV8Str(isolate, "width"))
                  .ToLocal(&sw) &&
              sw->IsNumber())
            root_w = sw->NumberValue(context).FromMaybe(1024.0);
          if (screen.As<v8::Object>()
                  ->Get(context, ToV8Str(isolate, "height"))
                  .ToLocal(&sh) &&
              sh->IsNumber())
            root_h = sh->NumberValue(context).FromMaybe(768.0);
        }
      }
      double left = StyleNumber(isolate, context, target, "left");
      double top = StyleNumber(isolate, context, target, "top");
      double w = StyleNumber(isolate, context, target, "width");
      double h = StyleNumber(isolate, context, target, "height");
      double overlap_w = std::max(
          0.0, std::min(left + w, root_w) - std::max(left, 0.0));
      double overlap_h = std::max(
          0.0, std::min(top + h, root_h) - std::max(top, 0.0));
      double area = w * h;
      double ratio = area > 0 ? (overlap_w * overlap_h) / area : 0.0;
      bool intersecting = overlap_w > 0 && overlap_h > 0;
      v8::Local<v8::Object> entry = v8::Object::New(isolate);
      (void)entry->Set(context, ToV8Str(isolate, "target"), target)
          .FromMaybe(false);
      (void)entry
          ->Set(context, ToV8Str(isolate, "isIntersecting"),
                v8::Boolean::New(isolate, intersecting))
          .FromMaybe(false);
      (void)entry
          ->Set(context, ToV8Str(isolate, "intersectionRatio"),
                v8::Number::New(isolate, intersecting ? ratio : 0.0))
          .FromMaybe(false);
      (void)entry
          ->Set(context, ToV8Str(isolate, "rootBounds"),
                MakeDomRect(isolate, context, 0, 0, root_w, root_h))
          .FromMaybe(false);
      (void)entry
          ->Set(context, ToV8Str(isolate, "intersectionRect"),
                MakeDomRect(isolate, context, std::max(left, 0.0),
                         std::max(top, 0.0), overlap_w, overlap_h))
          .FromMaybe(false);
      (void)entry
          ->Set(context, ToV8Str(isolate, "boundingClientRect"),
                MakeDomRect(isolate, context, left, top, w, h))
          .FromMaybe(false);
      (void)entry
          ->Set(context, ToV8Str(isolate, "time"),
                v8::Number::New(isolate,
                                LogicalClock(isolate, context)))
          .FromMaybe(false);
      QueueObserverCallback(isolate, context, observer, entry);
    }

    bool DomObserverHasTarget(v8::Isolate* isolate,
                              const ContextState::DomObserver& observer,
                              v8::Local<v8::Object> target)
    {
      for (const auto& held : observer.targets)
      {
        if (held.Get(isolate)->SameValue(target)) return true;
      }
      return false;
    }

    void NotifyResize(const v8::FunctionCallbackInfo<v8::Value>& info)
    {
      v8::Isolate* isolate = info.GetIsolate();
      v8::Local<v8::Context> context = isolate->GetCurrentContext();
      ContextState* state = GetState(context);
      if (state == nullptr || info.Length() < 1 || !info[0]->IsObject())
        return;
      v8::Local<v8::Object> target = info[0].As<v8::Object>();
      // no registry mutation happens during the loop (fires only queue
      // microtasks), so direct indexed iteration is safe
      for (size_t i = 0; i < state->dom_observers.size(); ++i)
      {
        const ContextState::DomObserver& observer = state->dom_observers[i];
        if (!DomObserverHasTarget(isolate, observer, target)) continue;
        if (observer.intersection)
          FireIntersection(isolate, context, state, observer, target);
        else
          FireResize(isolate, context, observer, target);
      }
    }

    void DomObserverCtorCommon(
        const v8::FunctionCallbackInfo<v8::Value>& info, bool intersection)
    {
      v8::Isolate* isolate = info.GetIsolate();
      v8::Local<v8::Context> context = isolate->GetCurrentContext();
      ContextState* state = GetState(context);
      if (!info.IsConstructCall() || state == nullptr)
      {
        isolate->ThrowException(ToV8Str(
            isolate,
            intersection
                ? "TypeError: IntersectionObserver constructor requires new"
                : "TypeError: ResizeObserver constructor requires new"));
        return;
      }
      if (info.Length() < 1)
      {
        // M12-argreq: reference webidl 0-arg messages (the IntersectionObserver
        // one carries the reference's doubled construct prefix, verbatim)
        isolate->ThrowException(v8::Exception::TypeError(ToV8Str(
            isolate,
            intersection
                ? "Failed to construct 'IntersectionObserver': Failed to "
                  "construct 'IntersectionObserver': 1 argument required, "
                  "but only 0 present."
                : "Failed to construct 'ResizeObserver': 1 argument "
                  "required, but only 0 present.")));
        return;
      }
      if (!info[0]->IsFunction())
      {
        isolate->ThrowException(
            ToV8Str(isolate, "TypeError: callback must be a function"));
        return;
      }
      ContextState::DomObserver observer;
      observer.instance.Reset(isolate, info.This());
      observer.callback.Reset(isolate, info[0].As<v8::Function>());
      observer.intersection = intersection;
      state->dom_observers.push_back(std::move(observer));
      if (intersection && info.Length() >= 2 && info[1]->IsObject())
      {
        v8::Local<v8::Object> options = info[1].As<v8::Object>();
        v8::Local<v8::Value> thresholds;
        if (options->Get(context, ToV8Str(isolate, "threshold"))
                .ToLocal(&thresholds) &&
            !thresholds->IsUndefined())
          (void)info.This()
              ->Set(context, ToV8Str(isolate, "thresholds"), thresholds)
              .FromMaybe(false);
        else
          (void)info.This()
              ->Set(context, ToV8Str(isolate, "thresholds"),
                    v8::Array::New(isolate, 1))
              .FromMaybe(false);
        v8::Local<v8::Value> root;
        if (options->Get(context, ToV8Str(isolate, "root"))
                .ToLocal(&root) &&
            !root->IsUndefined())
          (void)info.This()
              ->Set(context, ToV8Str(isolate, "root"), root)
              .FromMaybe(false);
        else
          (void)info.This()
              ->Set(context, ToV8Str(isolate, "root"), v8::Null(isolate))
              .FromMaybe(false);
        v8::Local<v8::Value> root_margin;
        if (options->Get(context, ToV8Str(isolate, "rootMargin"))
                .ToLocal(&root_margin) &&
            !root_margin->IsUndefined())
          (void)info.This()
              ->Set(context, ToV8Str(isolate, "rootMargin"), root_margin)
              .FromMaybe(false);
        else
          (void)info.This()
              ->Set(context, ToV8Str(isolate, "rootMargin"),
                    ToV8Str(isolate, "0px"))
              .FromMaybe(false);
      }
    }

    void ResizeObserverCtor(
        const v8::FunctionCallbackInfo<v8::Value>& info)
    {
      DomObserverCtorCommon(info, false);
    }

    void IntersectionObserverCtor(
        const v8::FunctionCallbackInfo<v8::Value>& info)
    {
      DomObserverCtorCommon(info, true);
    }

    // shared observe: register the target then fire the initial entry
    void DomObserverObserve(
        const v8::FunctionCallbackInfo<v8::Value>& info)
    {
      v8::Isolate* isolate = info.GetIsolate();
      v8::Local<v8::Context> context = isolate->GetCurrentContext();
      ContextState* state = GetState(context);
      if (state == nullptr || info.Length() < 1 || !info[0]->IsObject())
        return;
      v8::Local<v8::Object> target = info[0].As<v8::Object>();
      for (auto& observer : state->dom_observers)
      {
        if (!observer.instance.Get(isolate)->SameValue(info.This())) continue;
        if (!DomObserverHasTarget(isolate, observer, target))
          observer.targets.emplace_back(isolate, target);
        if (observer.intersection)
          FireIntersection(isolate, context, state, observer, target);
        else
          FireResize(isolate, context, observer, target);
        return;
      }
    }

    void DomObserverUnobserve(
        const v8::FunctionCallbackInfo<v8::Value>& info)
    {
      v8::Isolate* isolate = info.GetIsolate();
      v8::Local<v8::Context> context = isolate->GetCurrentContext();
      ContextState* state = GetState(context);
      if (state == nullptr || info.Length() < 1 || !info[0]->IsObject())
        return;
      v8::Local<v8::Object> target = info[0].As<v8::Object>();
      for (auto& observer : state->dom_observers)
      {
        if (!observer.instance.Get(isolate)->SameValue(info.This())) continue;
        for (auto it = observer.targets.begin();
             it != observer.targets.end(); ++it)
        {
          if (it->Get(isolate)->SameValue(target))
          {
            observer.targets.erase(it);
            break;
          }
        }
        return;
      }
    }

    void DomObserverDisconnect(
        const v8::FunctionCallbackInfo<v8::Value>& info)
    {
      v8::Isolate* isolate = info.GetIsolate();
      v8::Local<v8::Context> context = isolate->GetCurrentContext();
      ContextState* state = GetState(context);
      if (state == nullptr) return;
      for (size_t i = 0; i < state->dom_observers.size(); i++)
      {
        if (state->dom_observers[i].instance.Get(isolate)->SameValue(info.This()))
        {
          state->dom_observers.erase(state->dom_observers.begin() + i);
          return;
        }
      }
    }

    void DomObserverTakeRecords(
        const v8::FunctionCallbackInfo<v8::Value>& info)
    {
      info.GetReturnValue().Set(v8::Array::New(info.GetIsolate(), 0));
    }

    void InstallResizeIntersectionObservers(
        v8::Isolate* isolate, v8::Local<v8::Context> context,
        ContextState* state)
    {
      auto install = [&](const char* name, bool intersection) {
        v8::Local<v8::FunctionTemplate> ctor = v8::FunctionTemplate::New(
            isolate, intersection ? IntersectionObserverCtor
                                  : ResizeObserverCtor);
        ctor->SetClassName(ToV8Str(isolate, name));
        v8::Local<v8::ObjectTemplate> inst = ctor->InstanceTemplate();
        inst->SetInternalFieldCount(0);
        inst->Set(v8::Symbol::GetToStringTag(isolate), ToV8Str(isolate, name),
                  v8::PropertyAttribute::ReadOnly);
        auto method = [&](const char* mname, v8::FunctionCallback cb) {
          v8::Local<v8::String> key = ToV8Str(isolate, mname);
          v8::Local<v8::FunctionTemplate> fn =
              v8::FunctionTemplate::New(isolate, cb);
          fn->SetClassName(key);
          ctor->PrototypeTemplate()->Set(key, fn);
        };
        method("observe", DomObserverObserve);
        method("unobserve", DomObserverUnobserve);
        method("disconnect", DomObserverDisconnect);
        if (intersection) method("takeRecords", DomObserverTakeRecords);
        (void)context
            ->Global()
            ->DefineOwnProperty(context, ToV8Str(isolate, name),
                                ctor->GetFunction(context).ToLocalChecked(),
                                static_cast<v8::PropertyAttribute>(v8::PropertyAttribute::DontEnum))
            .FromMaybe(false);
      };
      install("ResizeObserver", false);
      install("IntersectionObserver", true);
      v8::Local<v8::FunctionTemplate> notify =
          v8::FunctionTemplate::New(isolate, NotifyResize);
      notify->SetClassName(ToV8Str(isolate, "__lv8_notifyResize"));
      (void)context
          ->Global()
          ->DefineOwnProperty(context,
                              ToV8Str(isolate, "__lv8_notifyResize"),
                              notify->GetFunction(context).ToLocalChecked(),
                              static_cast<v8::PropertyAttribute>(v8::PropertyAttribute::DontEnum))
          .FromMaybe(false);
    }

    void InstallMutationObserver(v8::Isolate* isolate,
                                 v8::Local<v8::Context> context,
                                 ContextState* state)
    {
      if (!state->mutationobserver_tpl.IsEmpty()) return;
      v8::Local<v8::FunctionTemplate> ctor = v8::FunctionTemplate::New(
          isolate, MutationObserverConstructor);
      ctor->SetClassName(ToV8Str(isolate, "MutationObserver"));
      v8::Local<v8::ObjectTemplate> inst = ctor->InstanceTemplate();
      inst->SetInternalFieldCount(0);
      // M10: zero own symbols on instances (tag on prototype)
      ctor->PrototypeTemplate()
          ->Set(v8::Symbol::GetToStringTag(isolate),
                ToV8Str(isolate, "MutationObserver"),
                static_cast<v8::PropertyAttribute>(
                    v8::PropertyAttribute::ReadOnly |
                    v8::PropertyAttribute::DontEnum));
      auto method = [&](const char* name, v8::FunctionCallback cb) {
        v8::Local<v8::String> key = ToV8Str(isolate, name);
        v8::Local<v8::FunctionTemplate> fn =
            v8::FunctionTemplate::New(isolate, cb);
        fn->SetClassName(key);
        ctor->PrototypeTemplate()->Set(key, fn);
      };
      method("observe", MutationObserverObserve);
      method("disconnect", MutationObserverDisconnect);
      method("takeRecords", MutationObserverTakeRecords);
      state->mutationobserver_tpl.Reset(isolate, ctor);
      (void)context
          ->Global()
          ->DefineOwnProperty(context, ToV8Str(isolate, "MutationObserver"),
                              ctor->GetFunction(context).ToLocalChecked(),
                              static_cast<v8::PropertyAttribute>(v8::PropertyAttribute::DontEnum))
          .FromMaybe(false);
      v8::Local<v8::FunctionTemplate> notify = v8::FunctionTemplate::New(
          isolate, MoNotify);
      notify->SetClassName(ToV8Str(isolate, "__lv8_notifyMutation"));
      (void)context
          ->Global()
          ->DefineOwnProperty(context,
                              ToV8Str(isolate, "__lv8_notifyMutation"),
                              notify->GetFunction(context).ToLocalChecked(),
                              static_cast<v8::PropertyAttribute>(v8::PropertyAttribute::DontEnum))
          .FromMaybe(false);
    }

  void InstallDomExtras(v8::Isolate* isolate, v8::Local<v8::Context> context)
  {
    v8::Local<v8::Object> global = context->Global();

    // Node constants
    {
      v8::Local<v8::Object> node = v8::Object::New(isolate);
      auto set_const = [&](const char* name, double value) {
        (void)node->Set(context, ToV8Str(isolate, name),
                        v8::Number::New(isolate, value)).FromMaybe(false);
      };
      set_const("ELEMENT_NODE", 1);
      set_const("TEXT_NODE", 3);
      set_const("COMMENT_NODE", 8);
      set_const("DOCUMENT_NODE", 9);
      set_const("DOCUMENT_FRAGMENT_NODE", 11);
      set_const("ATTRIBUTE_NODE", 2);
      set_const("CDATA_SECTION_NODE", 4);
      set_const("PROCESSING_INSTRUCTION_NODE", 7);
      set_const("DOCUMENT_TYPE_NODE", 10);
      // containment / position
      node->Set(context, ToV8Str(isolate, "DOCUMENT_POSITION_DISCONNECTED"),
                v8::Integer::New(isolate, 1)).FromMaybe(false);
      node->Set(context, ToV8Str(isolate, "DOCUMENT_POSITION_PRECEDING"),
                v8::Integer::New(isolate, 2)).FromMaybe(false);
      node->Set(context, ToV8Str(isolate, "DOCUMENT_POSITION_FOLLOWING"),
                v8::Integer::New(isolate, 4)).FromMaybe(false);
      node->Set(context, ToV8Str(isolate, "DOCUMENT_POSITION_CONTAINS"),
                v8::Integer::New(isolate, 8)).FromMaybe(false);
      node->Set(context, ToV8Str(isolate, "DOCUMENT_POSITION_CONTAINED_BY"),
                v8::Integer::New(isolate, 16)).FromMaybe(false);
      // Node.prototype.contains
      v8::Local<v8::FunctionTemplate> tpl = v8::FunctionTemplate::New(isolate);
      (void)context->Global()
          ->DefineOwnProperty(context, ToV8Str(isolate, "Node"), node,
                              static_cast<v8::PropertyAttribute>(v8::PropertyAttribute::DontEnum))
          .FromMaybe(false);
    }

    // DOMException constructor
    {
      v8::Local<v8::FunctionTemplate> tpl = v8::FunctionTemplate::New(
          isolate,
          [](const v8::FunctionCallbackInfo<v8::Value>& info) {
            v8::Isolate* iso = info.GetIsolate();
            v8::Local<v8::Context> ctx = iso->GetCurrentContext();
            std::string msg = info.Length() >= 1 && !info[0]->IsUndefined()
                ? ToUtf8(iso, info[0]) : "";
            std::string name = info.Length() >= 2 && info[1]->IsString()
                ? ToUtf8(iso, info[1]) : "Error";
            (void)info.This()->Set(ctx, ToV8Str(iso, "message"),
                                   ToV8Str(iso, msg)).FromMaybe(false);
            (void)info.This()->Set(ctx, ToV8Str(iso, "name"),
                                   ToV8Str(iso, name)).FromMaybe(false);
            // well-known codes
            int32_t code = 0;
            if (name == "NotFoundError") code = 8;
            else if (name == "SecurityError") code = 18;
            else if (name == "SyntaxError") code = 12;
            else if (name == "InvalidStateError") code = 11;
            else if (name == "NotSupportedError") code = 9;
            (void)info.This()->Set(ctx, ToV8Str(iso, "code"),
                                   v8::Integer::New(iso, code))
                .FromMaybe(false);
          });
      tpl->SetClassName(ToV8Str(isolate, "DOMException"));
      v8::Local<v8::ObjectTemplate> inst = tpl->InstanceTemplate();
      inst->SetInternalFieldCount(0);
      // M10: zero own symbols on instances (tag on prototype)
      tpl->PrototypeTemplate()
          ->Set(v8::Symbol::GetToStringTag(isolate),
                ToV8Str(isolate, "DOMException"),
                static_cast<v8::PropertyAttribute>(
                    v8::PropertyAttribute::ReadOnly |
                    v8::PropertyAttribute::DontEnum));
      // DOMException.NOT_FOUND_ERR = 8
      (void)tpl->Set(isolate, "NOT_FOUND_ERR",
                     v8::Integer::New(isolate, 8),
                     v8::PropertyAttribute::ReadOnly);
      (void)tpl->Set(isolate, "SECURITY_ERR",
                     v8::Integer::New(isolate, 18),
                     v8::PropertyAttribute::ReadOnly);
      (void)context
          ->Global()
          ->DefineOwnProperty(context, ToV8Str(isolate, "DOMException"),
                              tpl->GetFunction(context).ToLocalChecked(),
                              static_cast<v8::PropertyAttribute>(v8::PropertyAttribute::DontEnum))
          .FromMaybe(false);
      // DOMException instances are Errors (instanceof chain)
      {
        v8::Local<v8::Function> ctor =
            tpl->GetFunction(context).ToLocalChecked();
        v8::Local<v8::Value> de_proto, error_fn;
        if (ctor->Get(context, ToV8Str(isolate, "prototype"))
                .ToLocal(&de_proto) &&
            de_proto->IsObject() &&
            context->Global()
                ->Get(context, ToV8Str(isolate, "Error"))
                .ToLocal(&error_fn) &&
            error_fn->IsFunction())
        {
          v8::Local<v8::Value> err_proto;
          if (error_fn.As<v8::Function>()
                  ->Get(context, ToV8Str(isolate, "prototype"))
                  .ToLocal(&err_proto) &&
              err_proto->IsObject())
          {
            (void)de_proto.As<v8::Object>()
                ->SetPrototype(context, err_proto)
                .FromMaybe(false);
          }
        }
      }
    }

    // trustedTypes
    {
      v8::Local<v8::Object> tt = v8::Object::New(isolate);
      // policy registry rides as a private so it never leaks to JS
      v8::Local<v8::Object> policies = v8::Object::New(isolate);
      (void)tt->SetPrivate(context,
                           v8::Private::ForApi(
                               isolate, ToV8Str(isolate, "lv8.tt.registry")),
                           policies)
          .FromMaybe(false);

      // createPolicy(name, {createHTML, createScript, createScriptURL})
      v8::Local<v8::String> cp_key = ToV8Str(isolate, "createPolicy");
      v8::Local<v8::FunctionTemplate> cp_fn = v8::FunctionTemplate::New(
          isolate,
          [](const v8::FunctionCallbackInfo<v8::Value>& info) {
            v8::Isolate* iso = info.GetIsolate();
            v8::Local<v8::Context> ctx = iso->GetCurrentContext();
            if (info.Length() < 1 || !info[0]->IsString()) return;
            std::string pname = ToUtf8(iso, info[0]);
            v8::Local<v8::Object> policy = v8::Object::New(iso);
            (void)policy->Set(ctx, ToV8Str(iso, "name"),
                              ToV8Str(iso, pname))
                .FromMaybe(false);
            if (info.Length() > 1 && info[1]->IsObject())
            {
              v8::Local<v8::Object> config = info[1].As<v8::Object>();
              for (const char* m :
                   {"createHTML", "createScript", "createScriptURL"})
              {
                v8::Local<v8::Value> callback;
                if (!config->Get(ctx, ToV8Str(iso, m)).ToLocal(&callback) ||
                    !callback->IsFunction())
                  continue;
                // keep the user callback on the policy; the visible method
                // is a native wrapper that brands the callback's result
                (void)policy
                    ->SetPrivate(
                        ctx,
                        v8::Private::ForApi(
                            iso,
                            ToV8Str(iso, std::string("lv8.tt.user.") + m)),
                        callback)
                    .FromMaybe(false);
                v8::Local<v8::String> mkey = ToV8Str(iso, m);
                v8::Local<v8::FunctionTemplate> wrap_tpl =
                    v8::FunctionTemplate::New(
                        iso,
                        [](const v8::FunctionCallbackInfo<v8::Value>& info) {
                          v8::Isolate* iso2 = info.GetIsolate();
                          v8::Local<v8::Context> ctx2 =
                              iso2->GetCurrentContext();
                          v8::String::Utf8Value kind_v(iso2, info.Data());
                          std::string kind(*kind_v, kind_v.length());
                          v8::Local<v8::Value> user;
                          if (!info.This()
                                       ->GetPrivate(
                                           ctx2,
                                           v8::Private::ForApi(
                                               iso2,
                                               ToV8Str(
                                                   iso2,
                                                   "lv8.tt.user." + kind)))
                                       .ToLocal(&user) ||
                              !user->IsFunction())
                            return;
                          std::vector<v8::Local<v8::Value>> args;
                          for (int i = 0; i < info.Length(); i++)
                            args.push_back(info[i]);
                          v8::Local<v8::Value> out;
                          v8::TryCatch try_catch(iso2);
                          if (!user.As<v8::Function>()
                                   ->Call(ctx2, info.This(),
                                          static_cast<int>(args.size()),
                                          args.empty() ? nullptr
                                                       : args.data())
                                   .ToLocal(&out))
                            CJavascriptException::ThrowIf(iso2, try_catch);
                          // box into a String object and brand it
                          v8::Local<v8::String> text = out->IsString()
                              ? out.As<v8::String>()
                              : ToV8Str(iso2, "");
                          v8::Local<v8::Value> branded =
                              v8::StringObject::New(iso2, text);
                          (void)branded.As<v8::Object>()
                              ->SetPrivate(
                                  ctx2,
                                  v8::Private::ForApi(
                                      iso2,
                                      ToV8Str(iso2, "lv8.tt.brand." + kind)),
                                  v8::Boolean::New(iso2, true))
                              .FromMaybe(false);
                          info.GetReturnValue().Set(branded);
                        },
                        mkey);
                wrap_tpl->SetClassName(mkey);
                (void)policy
                    ->Set(ctx, mkey,
                          wrap_tpl->GetFunction(ctx).ToLocalChecked())
                    .FromMaybe(false);
              }
            }
            // register for getPolicyNames()
            v8::Local<v8::Value> registry;
            if (info.This()
                    ->GetPrivate(
                        ctx,
                        v8::Private::ForApi(
                            iso, ToV8Str(iso, "lv8.tt.registry")))
                    .ToLocal(&registry) &&
                registry->IsObject())
            {
              (void)registry.As<v8::Object>()
                  ->Set(ctx, ToV8Str(iso, pname), policy)
                  .FromMaybe(false);
            }
            info.GetReturnValue().Set(policy);
          });
      cp_fn->SetClassName(cp_key);
      (void)tt->Set(context, cp_key,
                    cp_fn->GetFunction(context).ToLocalChecked())
          .FromMaybe(false);

      // isHTML / isScript / isScriptURL: check the matching brand kind
      auto is_fn = [&](const char* name, const char* kind) {
        v8::Local<v8::String> key = ToV8Str(isolate, name);
        v8::Local<v8::FunctionTemplate> fn = v8::FunctionTemplate::New(
            isolate,
            [](const v8::FunctionCallbackInfo<v8::Value>& info) {
              v8::Isolate* iso = info.GetIsolate();
              v8::Local<v8::Context> ctx = iso->GetCurrentContext();
              bool result = false;
              if (info.Length() >= 1 && info[0]->IsObject() &&
                  info.Data()->IsString())
              {
                v8::String::Utf8Value kind_v(iso, info.Data());
                std::string kind(*kind_v, kind_v.length());
                v8::Local<v8::Value> branded;
                if (info[0]
                        .As<v8::Object>()
                        ->GetPrivate(
                            ctx,
                            v8::Private::ForApi(
                                iso, ToV8Str(iso, "lv8.tt.brand." + kind)))
                        .ToLocal(&branded) &&
                    branded->IsTrue())
                  result = true;
              }
              info.GetReturnValue().Set(v8::Boolean::New(iso, result));
            },
            ToV8Str(isolate, kind));
        fn->SetClassName(key);
        (void)tt->Set(context, key,
                      fn->GetFunction(context).ToLocalChecked())
            .FromMaybe(false);
      };
      is_fn("isHTML", "createHTML");
      is_fn("isScript", "createScript");
      is_fn("isScriptURL", "createScriptURL");

      // getPolicyNames()
      v8::Local<v8::String> gpn_key = ToV8Str(isolate, "getPolicyNames");
      v8::Local<v8::FunctionTemplate> gpn_fn = v8::FunctionTemplate::New(
          isolate,
          [](const v8::FunctionCallbackInfo<v8::Value>& info) {
            v8::Isolate* iso = info.GetIsolate();
            v8::Local<v8::Context> ctx = iso->GetCurrentContext();
            v8::Local<v8::Value> p;
            v8::Local<v8::Array> names = v8::Array::New(iso);
            if (info.This()
                    ->GetPrivate(
                        ctx,
                        v8::Private::ForApi(
                            iso, ToV8Str(iso, "lv8.tt.registry")))
                    .ToLocal(&p) && p->IsObject())
            {
              v8::Local<v8::Array> keys =
                  p.As<v8::Object>()
                      ->GetOwnPropertyNames(ctx).ToLocalChecked();
              for (uint32_t i = 0; i < keys->Length(); i++)
              {
                (void)names
                    ->Set(ctx, i,
                          keys->Get(ctx, i).FromMaybe(
                              v8::Local<v8::Value>()))
                    .FromMaybe(false);
              }
            }
            info.GetReturnValue().Set(names);
          });
      gpn_fn->SetClassName(gpn_key);
      (void)tt->Set(context, gpn_key,
                    gpn_fn->GetFunction(context).ToLocalChecked())
          .FromMaybe(false);

      (void)context
          ->Global()
          ->DefineOwnProperty(context, ToV8Str(isolate, "trustedTypes"), tt,
                              static_cast<v8::PropertyAttribute>(0))
          .FromMaybe(false);
    }

    // ---- G4 ledger chunk 1: geometry + DataTransfer globals ----------
    {
      ContextState* state = GetState(context);
      auto register_global = [&](const char* name,
                                 v8::Local<v8::FunctionTemplate> ctor) {
        (void)global
            ->DefineOwnProperty(context, ToV8Str(isolate, name),
                                ctor->GetFunction(context).ToLocalChecked(),
                                static_cast<v8::PropertyAttribute>(v8::PropertyAttribute::DontEnum))
            .FromMaybe(false);
      };
      if (state->domrect_tpl.IsEmpty())
      {
        v8::Local<v8::FunctionTemplate> ctor = v8::FunctionTemplate::New(
            isolate, DomRectConstructor);
        ctor->SetClassName(ToV8Str(isolate, "DOMRect"));
        v8::Local<v8::ObjectTemplate> inst = ctor->InstanceTemplate();
        inst->SetInternalFieldCount(4);
        // M10: zero own symbols on instances (tag on prototype)
        ctor->PrototypeTemplate()
            ->Set(v8::Symbol::GetToStringTag(isolate),
                  ToV8Str(isolate, "DOMRect"),
                  static_cast<v8::PropertyAttribute>(
                      v8::PropertyAttribute::ReadOnly |
                      v8::PropertyAttribute::DontEnum));
        auto getter = [&](const char* name, v8::FunctionCallback cb) {
          v8::Local<v8::FunctionTemplate> fn =
              v8::FunctionTemplate::New(isolate, cb);
          fn->SetClassName(ToV8Str(isolate, std::string("get ") + name));
          ctor->PrototypeTemplate()->SetAccessorProperty(
              ToV8Str(isolate, name), fn,
              v8::Local<v8::FunctionTemplate>());
        };
        getter("x", RectX);
        getter("y", RectY);
        getter("width", RectWidth);
        getter("height", RectHeight);
        getter("left", RectLeft);
        getter("right", RectRight);
        getter("top", RectTop);
        getter("bottom", RectBottom);
        auto method = [&](const char* name, v8::FunctionCallback cb) {
          v8::Local<v8::String> key = ToV8Str(isolate, name);
          v8::Local<v8::FunctionTemplate> fn =
              v8::FunctionTemplate::New(isolate, cb);
          fn->SetClassName(key);
          ctor->PrototypeTemplate()->Set(key, fn);
        };
        method("toJSON", RectToJSON);
        ctor->Set(isolate, "fromRect",
                  v8::FunctionTemplate::New(isolate, RectFromRect),
                  v8::PropertyAttribute::DontEnum);
        state->domrect_tpl.Reset(isolate, ctor);
        register_global("DOMRect", ctor);
      }

      if (state->dompointro_tpl.IsEmpty())
      {
        v8::Local<v8::FunctionTemplate> ctor = v8::FunctionTemplate::New(
            isolate, PointReadOnlyConstructor);
        ctor->SetClassName(ToV8Str(isolate, "DOMPointReadOnly"));
        v8::Local<v8::ObjectTemplate> inst = ctor->InstanceTemplate();
        // M12: 4 internal fields carry the geometry state (zero own)
        inst->SetInternalFieldCount(4);
        // M10: zero own symbols on instances (tag on prototype)
        ctor->PrototypeTemplate()
            ->Set(v8::Symbol::GetToStringTag(isolate),
                  ToV8Str(isolate, "DOMPointReadOnly"),
                  static_cast<v8::PropertyAttribute>(
                      v8::PropertyAttribute::ReadOnly |
                      v8::PropertyAttribute::DontEnum));
        auto getter = [&](const char* name, v8::FunctionCallback cb) {
          v8::Local<v8::FunctionTemplate> fn =
              v8::FunctionTemplate::New(isolate, cb);
          fn->SetClassName(ToV8Str(isolate, std::string("get ") + name));
          ctor->PrototypeTemplate()->SetAccessorProperty(
              ToV8Str(isolate, name), fn,
              v8::Local<v8::FunctionTemplate>());
        };
        getter("x", PointGetX);
        getter("y", PointGetY);
        getter("z", PointGetZ);
        getter("w", PointGetW);
        auto method = [&](const char* name, v8::FunctionCallback cb) {
          v8::Local<v8::String> key = ToV8Str(isolate, name);
          v8::Local<v8::FunctionTemplate> fn =
              v8::FunctionTemplate::New(isolate, cb);
          fn->SetClassName(key);
          ctor->PrototypeTemplate()->Set(key, fn);
        };
        method("matrixTransform", PointMatrixTransform);
        method("toJSON", PointToJSON);
        ctor->Set(isolate, "fromPoint",
                  v8::FunctionTemplate::New(isolate, PointRoFromPoint),
                  v8::PropertyAttribute::DontEnum);
        state->dompointro_tpl.Reset(isolate, ctor);
        register_global("DOMPointReadOnly", ctor);
      }

      if (state->dompoint_tpl.IsEmpty())
      {
        v8::Local<v8::FunctionTemplate> ctor = v8::FunctionTemplate::New(
            isolate, PointConstructor);
        ctor->SetClassName(ToV8Str(isolate, "DOMPoint"));
        v8::Local<v8::ObjectTemplate> inst = ctor->InstanceTemplate();
        // M12: 4 internal fields carry the geometry state (zero own)
        inst->SetInternalFieldCount(4);
        // M10: zero own symbols on instances (tag on prototype)
        ctor->PrototypeTemplate()
            ->Set(v8::Symbol::GetToStringTag(isolate),
                  ToV8Str(isolate, "DOMPoint"),
                  static_cast<v8::PropertyAttribute>(
                      v8::PropertyAttribute::ReadOnly |
                      v8::PropertyAttribute::DontEnum));
        ctor->Inherit(state->dompointro_tpl.Get(isolate));
        // readable + writable x/y/z/w shadow the read-only getters
        auto access = [&](const char* name, v8::FunctionCallback get,
                          v8::FunctionCallback set) {
          v8::Local<v8::FunctionTemplate> get_fn =
              v8::FunctionTemplate::New(isolate, get);
          get_fn->SetClassName(ToV8Str(isolate, std::string("get ") + name));
          v8::Local<v8::FunctionTemplate> set_fn =
              v8::FunctionTemplate::New(isolate, set);
          set_fn->SetClassName(ToV8Str(isolate, std::string("set ") + name));
          ctor->PrototypeTemplate()->SetAccessorProperty(
              ToV8Str(isolate, name), get_fn, set_fn);
        };
        access("x", PointGetX, PointSetX);
        access("y", PointGetY, PointSetY);
        access("z", PointGetZ, PointSetZ);
        access("w", PointGetW, PointSetW);
        ctor->Set(isolate, "fromPoint",
                  v8::FunctionTemplate::New(isolate, PointFromPoint),
                  v8::PropertyAttribute::DontEnum);
        state->dompoint_tpl.Reset(isolate, ctor);
        register_global("DOMPoint", ctor);
      }

      if (state->dommatrix_tpl.IsEmpty())
      {
        v8::Local<v8::FunctionTemplate> ro = v8::FunctionTemplate::New(
            isolate, MatrixReadOnlyConstructor);
        ro->SetClassName(ToV8Str(isolate, "DOMMatrixReadOnly"));
        v8::Local<v8::ObjectTemplate> inst = ro->InstanceTemplate();
        inst->SetInternalFieldCount(0);
        // M10: zero own symbols on instances (tag on prototype)
        ro->PrototypeTemplate()
            ->Set(v8::Symbol::GetToStringTag(isolate),
                  ToV8Str(isolate, "DOMMatrixReadOnly"),
                  static_cast<v8::PropertyAttribute>(
                      v8::PropertyAttribute::ReadOnly |
                      v8::PropertyAttribute::DontEnum));
        auto method = [&](v8::Local<v8::FunctionTemplate> target,
                          const char* name, v8::FunctionCallback cb) {
          v8::Local<v8::String> key = ToV8Str(isolate, name);
          v8::Local<v8::FunctionTemplate> fn =
              v8::FunctionTemplate::New(isolate, cb);
          fn->SetClassName(key);
          target->PrototypeTemplate()->Set(key, fn);
        };
        method(ro, "multiply", MatrixMultiply);
        method(ro, "translate", MatrixTranslate);
        method(ro, "scale", MatrixScale);
        method(ro, "scaleNonUniform", MatrixScaleNonUniform);
        method(ro, "rotate", MatrixRotate);
        method(ro, "inverse", MatrixInverse);
        method(ro, "transformPoint", MatrixTransformPoint);
        method(ro, "toFloat32Array", MatrixToFloat32Array);
        method(ro, "toFloat64Array", MatrixToFloat64Array);
        method(ro, "toString", MatrixToString);
        ro->Set(isolate, "fromMatrix",
                v8::FunctionTemplate::New(isolate, MatrixRoFromMatrix),
                v8::PropertyAttribute::DontEnum);
        state->dommatrixro_tpl.Reset(isolate, ro);
        register_global("DOMMatrixReadOnly", ro);

        v8::Local<v8::FunctionTemplate> ctor = v8::FunctionTemplate::New(
            isolate, MatrixConstructor);
        ctor->SetClassName(ToV8Str(isolate, "DOMMatrix"));
        v8::Local<v8::ObjectTemplate> minst = ctor->InstanceTemplate();
        minst->SetInternalFieldCount(0);
        // M10: zero own symbols on instances (tag on prototype)
        ctor->PrototypeTemplate()
            ->Set(v8::Symbol::GetToStringTag(isolate),
                  ToV8Str(isolate, "DOMMatrix"),
                  static_cast<v8::PropertyAttribute>(
                      v8::PropertyAttribute::ReadOnly |
                      v8::PropertyAttribute::DontEnum));
        ctor->Inherit(ro);
        method(ctor, "multiplySelf", MatrixMultiplySelf);
        method(ctor, "preMultiplySelf", MatrixPreMultiplySelf);
        method(ctor, "translateSelf", MatrixTranslateSelf);
        method(ctor, "scaleSelf", MatrixScaleSelf);
        method(ctor, "scaleNonUniformSelf", MatrixScaleNonUniformSelf);
        method(ctor, "rotateSelf", MatrixRotateSelf);
        method(ctor, "invertSelf", MatrixInvertSelf);
        method(ctor, "setMatrixValue", MatrixSetMatrixValue);
        ctor->Set(isolate, "fromMatrix",
                  v8::FunctionTemplate::New(isolate, MatrixFromMatrix),
                  v8::PropertyAttribute::DontEnum);
        state->dommatrix_tpl.Reset(isolate, ctor);
        register_global("DOMMatrix", ctor);
      }

      if (state->domquad_tpl.IsEmpty())
      {
        v8::Local<v8::FunctionTemplate> ctor = v8::FunctionTemplate::New(
            isolate, QuadConstructor);
        ctor->SetClassName(ToV8Str(isolate, "DOMQuad"));
        v8::Local<v8::ObjectTemplate> inst = ctor->InstanceTemplate();
        // M12: 4 internal fields carry the geometry state (zero own)
        inst->SetInternalFieldCount(4);
        // M10: zero own symbols on instances (tag on prototype)
        ctor->PrototypeTemplate()
            ->Set(v8::Symbol::GetToStringTag(isolate),
                  ToV8Str(isolate, "DOMQuad"),
                  static_cast<v8::PropertyAttribute>(
                      v8::PropertyAttribute::ReadOnly |
                      v8::PropertyAttribute::DontEnum));
        auto quad_getter = [&](const char* name, v8::FunctionCallback cb) {
          v8::Local<v8::String> key = ToV8Str(isolate, name);
          v8::Local<v8::FunctionTemplate> fn =
              v8::FunctionTemplate::New(isolate, cb);
          fn->SetClassName(ToV8Str(isolate, std::string("get ") + name));
          ctor->PrototypeTemplate()->SetAccessorProperty(
              key, fn, v8::Local<v8::FunctionTemplate>());
        };
        quad_getter("p1", QuadGetP1);
        quad_getter("p2", QuadGetP2);
        quad_getter("p3", QuadGetP3);
        quad_getter("p4", QuadGetP4);
        v8::Local<v8::FunctionTemplate> bounds =
            v8::FunctionTemplate::New(isolate, QuadGetBounds);
        bounds->SetClassName(ToV8Str(isolate, "getBounds"));
        ctor->PrototypeTemplate()->Set(ToV8Str(isolate, "getBounds"),
                                       bounds);
        ctor->Set(isolate, "fromRect",
                  v8::FunctionTemplate::New(isolate, QuadFromRect),
                  v8::PropertyAttribute::DontEnum);
        ctor->Set(isolate, "fromQuad",
                  v8::FunctionTemplate::New(isolate, QuadFromQuad),
                  v8::PropertyAttribute::DontEnum);
        state->domquad_tpl.Reset(isolate, ctor);
        register_global("DOMQuad", ctor);
      }

      if (state->dt_item_tpl.IsEmpty())
      {
        v8::Local<v8::FunctionTemplate> ctor = v8::FunctionTemplate::New(
            isolate, DataTransferItemConstructor);
        ctor->SetClassName(ToV8Str(isolate, "DataTransferItem"));
        v8::Local<v8::ObjectTemplate> inst = ctor->InstanceTemplate();
        inst->SetInternalFieldCount(0);
        // M10: zero own symbols on instances (tag on prototype)
        ctor->PrototypeTemplate()
            ->Set(v8::Symbol::GetToStringTag(isolate),
                  ToV8Str(isolate, "DataTransferItem"),
                  static_cast<v8::PropertyAttribute>(
                      v8::PropertyAttribute::ReadOnly |
                      v8::PropertyAttribute::DontEnum));
        auto method = [&](const char* name, v8::FunctionCallback cb) {
          v8::Local<v8::String> key = ToV8Str(isolate, name);
          v8::Local<v8::FunctionTemplate> fn =
              v8::FunctionTemplate::New(isolate, cb);
          fn->SetClassName(key);
          ctor->PrototypeTemplate()->Set(key, fn);
        };
        method("getAsFile", DataTransferItemGetAsFile);
        method("getAsString", DataTransferItemGetAsString);
        method("webkitGetAsEntry", DataTransferItemWebkitGetAsEntry);
        state->dt_item_tpl.Reset(isolate, ctor);
        register_global("DataTransferItem", ctor);
      }

      if (state->dtil_tpl.IsEmpty())
      {
        v8::Local<v8::FunctionTemplate> ctor = v8::FunctionTemplate::New(
            isolate, DataTransferItemListConstructor);
        ctor->SetClassName(ToV8Str(isolate, "DataTransferItemList"));
        v8::Local<v8::ObjectTemplate> inst = ctor->InstanceTemplate();
        inst->SetInternalFieldCount(0);
        // M10: zero own symbols on instances (tag on prototype)
        ctor->PrototypeTemplate()
            ->Set(v8::Symbol::GetToStringTag(isolate),
                  ToV8Str(isolate, "DataTransferItemList"),
                  static_cast<v8::PropertyAttribute>(
                      v8::PropertyAttribute::ReadOnly |
                      v8::PropertyAttribute::DontEnum));
        auto method = [&](const char* name, v8::FunctionCallback cb) {
          v8::Local<v8::String> key = ToV8Str(isolate, name);
          v8::Local<v8::FunctionTemplate> fn =
              v8::FunctionTemplate::New(isolate, cb);
          fn->SetClassName(key);
          ctor->PrototypeTemplate()->Set(key, fn);
        };
        method("item", DataTransferListItem);
        method("add", DataTransferItemListAdd);
        method("remove", DataTransferItemListRemove);
        method("clear", DataTransferItemListClear);
        v8::Local<v8::FunctionTemplate> iterator_fn =
            v8::FunctionTemplate::New(isolate, DataTransferItemListIterator);
        iterator_fn->SetClassName(ToV8Str(isolate, "values"));
        ctor->PrototypeTemplate()->Set(v8::Symbol::GetIterator(isolate),
                                       iterator_fn);
        state->dtil_tpl.Reset(isolate, ctor);
        register_global("DataTransferItemList", ctor);
      }

      if (state->dt_tpl.IsEmpty())
      {
        v8::Local<v8::FunctionTemplate> ctor = v8::FunctionTemplate::New(
            isolate, DataTransferConstructor);
        ctor->SetClassName(ToV8Str(isolate, "DataTransfer"));
        v8::Local<v8::ObjectTemplate> inst = ctor->InstanceTemplate();
        inst->SetInternalFieldCount(0);
        // M10: zero own symbols on instances (tag on prototype)
        ctor->PrototypeTemplate()
            ->Set(v8::Symbol::GetToStringTag(isolate),
                  ToV8Str(isolate, "DataTransfer"),
                  static_cast<v8::PropertyAttribute>(
                      v8::PropertyAttribute::ReadOnly |
                      v8::PropertyAttribute::DontEnum));
        auto getter = [&](const char* name, v8::FunctionCallback cb) {
          v8::Local<v8::FunctionTemplate> fn =
              v8::FunctionTemplate::New(isolate, cb);
          fn->SetClassName(ToV8Str(isolate, std::string("get ") + name));
          ctor->PrototypeTemplate()->SetAccessorProperty(
              ToV8Str(isolate, name), fn,
              v8::Local<v8::FunctionTemplate>());
        };
        getter("types", DataTransferTypes);
        getter("files", DataTransferFiles);
        getter("items", DataTransferItems);
        auto method = [&](const char* name, v8::FunctionCallback cb) {
          v8::Local<v8::String> key = ToV8Str(isolate, name);
          v8::Local<v8::FunctionTemplate> fn =
              v8::FunctionTemplate::New(isolate, cb);
          fn->SetClassName(key);
          ctor->PrototypeTemplate()->Set(key, fn);
        };
        method("setData", DataTransferSetData);
        method("getData", DataTransferGetData);
        method("clearData", DataTransferClearData);
        method("setDragImage", DataTransferSetDragImage);
        state->dt_tpl.Reset(isolate, ctor);
        register_global("DataTransfer", ctor);
      }
    }
    // ---- end G4 ledger chunk 1 ---------------------------------------
    InstallCssTypedOm(isolate, context);
    InstallWebAudio(isolate, context, GetState(context));
    InstallComputedStyle(isolate, context, GetState(context));
    InstallMutationObserver(isolate, context, GetState(context));
    InstallResizeIntersectionObservers(isolate, context, GetState(context));

    // performance.now() for Web Animations logical clock
    // (already available via __lv8_clock_ms from the net installer)
  }
} // namespace LV8
