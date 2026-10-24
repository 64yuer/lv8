    // ---- G4 ledger chunk 7c: deterministic streams ---------------------
    // (ReadableStream/WritableStream/TransformStream + controllers),
    // formerly JS bootstrap classes. Chunks live in private arrays; the
    // reader/writer are per-call native objects closing over the stream.

    v8::Local<v8::Private> RsChunksKey(v8::Isolate* isolate)
    {
      return v8::Private::ForApi(isolate, ToV8Str(isolate, "lv8.rs.chunks"));
    }
    v8::Local<v8::Private> RsClosedKey(v8::Isolate* isolate)
    {
      return v8::Private::ForApi(isolate, ToV8Str(isolate, "lv8.rs.closed"));
    }
    v8::Local<v8::Private> RsErroredKey(v8::Isolate* isolate)
    {
      return v8::Private::ForApi(isolate, ToV8Str(isolate, "lv8.rs.errored"));
    }
    v8::Local<v8::Private> RsHasErroredKey(v8::Isolate* isolate)
    {
      return v8::Private::ForApi(isolate,
                                 ToV8Str(isolate, "lv8.rs.has_errored"));
    }
    v8::Local<v8::Private> RsControllerKey(v8::Isolate* isolate)
    {
      return v8::Private::ForApi(isolate,
                                 ToV8Str(isolate, "lv8.rs.controller"));
    }
    v8::Local<v8::Private> RsReaderKey(v8::Isolate* isolate)
    {
      return v8::Private::ForApi(isolate, ToV8Str(isolate, "lv8.rs.reader"));
    }
    v8::Local<v8::Private> RscStreamKey(v8::Isolate* isolate)
    {
      return v8::Private::ForApi(isolate,
                                 ToV8Str(isolate, "lv8.rsc.stream"));
    }
    v8::Local<v8::Private> WsUnderlyingKey(v8::Isolate* isolate)
    {
      return v8::Private::ForApi(isolate,
                                 ToV8Str(isolate, "lv8.ws.underlying"));
    }

    void RsEnqueue(v8::Isolate* isolate, v8::Local<v8::Context> context,
                   v8::Local<v8::Object> stream, v8::Local<v8::Value> chunk)
    {
      v8::Local<v8::Value> chunks;
      if (stream->GetPrivate(context, RsChunksKey(isolate)).ToLocal(&chunks) &&
          chunks->IsArray())
        (void)chunks.As<v8::Array>()
            ->Set(context, chunks.As<v8::Array>()->Length(), chunk)
            .FromMaybe(false);
    }

    void RsSetClosed(v8::Isolate* isolate, v8::Local<v8::Context> context,
                     v8::Local<v8::Object> stream, bool closed)
    {
      (void)stream
          ->SetPrivate(context, RsClosedKey(isolate),
                       v8::Boolean::New(isolate, closed))
          .FromMaybe(false);
    }

    // resolve a promise with {value, done}
    void ResolveValueDone(v8::Isolate* isolate,
                          v8::Local<v8::Context> context,
                          v8::Local<v8::Promise::Resolver> resolver,
                          v8::Local<v8::Value> value, bool done)
    {
      v8::Local<v8::Object> out = v8::Object::New(isolate);
      (void)out->Set(context, ToV8Str(isolate, "value"), value)
          .FromMaybe(false);
      (void)out->Set(context, ToV8Str(isolate, "done"),
                     v8::Boolean::New(isolate, done))
          .FromMaybe(false);
      (void)resolver->Resolve(context, out).FromMaybe(false);
    }

    void RsControllerEnqueue(const v8::FunctionCallbackInfo<v8::Value>& info)
    {
      v8::Isolate* isolate = info.GetIsolate();
      v8::Local<v8::Context> context = isolate->GetCurrentContext();
      if (info.Length() < 1) return;
      v8::Local<v8::Value> stream;
      if (!info.This()
                ->GetPrivate(context, RscStreamKey(isolate))
                .ToLocal(&stream) ||
          !stream->IsObject())
        return;
      RsEnqueue(isolate, context, stream.As<v8::Object>(), info[0]);
    }

    void RsControllerClose(const v8::FunctionCallbackInfo<v8::Value>& info)
    {
      v8::Isolate* isolate = info.GetIsolate();
      v8::Local<v8::Context> context = isolate->GetCurrentContext();
      v8::Local<v8::Value> stream;
      if (info.This()
              ->GetPrivate(context, RscStreamKey(isolate))
              .ToLocal(&stream) &&
          stream->IsObject())
        RsSetClosed(isolate, context, stream.As<v8::Object>(), true);
    }

    void RsControllerError(const v8::FunctionCallbackInfo<v8::Value>& info)
    {
      v8::Isolate* isolate = info.GetIsolate();
      v8::Local<v8::Context> context = isolate->GetCurrentContext();
      v8::Local<v8::Value> stream;
      if (!info.This()
                ->GetPrivate(context, RscStreamKey(isolate))
                .ToLocal(&stream) ||
          !stream->IsObject())
        return;
      v8::Local<v8::Value> reason;
      bool have = info.Length() >= 1;
      if (have && info[0]->IsObject())
        reason = info[0];
      if (!have)
      {
        v8::Local<v8::Value> error_ctor;
        if (context->Global()
                ->Get(context, ToV8Str(isolate, "Error"))
                .ToLocal(&error_ctor) &&
            error_ctor->IsFunction())
        {
          v8::Local<v8::Value> argv[] = {ToV8Str(isolate, "stream errored")};
          v8::Local<v8::Value> error;
          if (error_ctor.As<v8::Function>()
                  ->NewInstance(context, 1, argv)
                  .ToLocal(&error))
            reason = error;
        }
      }
      (void)stream.As<v8::Object>()
          ->SetPrivate(context, RsErroredKey(isolate),
                       reason.IsEmpty()
                           ? v8::Local<v8::Value>(v8::Null(isolate))
                           : reason)
          .FromMaybe(false);
      (void)stream.As<v8::Object>()
          ->SetPrivate(context, RsHasErroredKey(isolate),
                       v8::Boolean::New(isolate, true))
          .FromMaybe(false);
    }

    void ReadableStreamCancel(const v8::FunctionCallbackInfo<v8::Value>& info)
    {
      v8::Isolate* isolate = info.GetIsolate();
      v8::Local<v8::Context> context = isolate->GetCurrentContext();
      RsSetClosed(isolate, context, info.This(), true);
      v8::Local<v8::Promise::Resolver> resolver =
          v8::Promise::Resolver::New(context).ToLocalChecked();
      (void)resolver->Resolve(context, v8::Undefined(isolate))
          .FromMaybe(false);
      info.GetReturnValue().Set(resolver->GetPromise());
    }

    void ReadableStreamGetReader(
        const v8::FunctionCallbackInfo<v8::Value>& info)
    {
      v8::Isolate* isolate = info.GetIsolate();
      v8::Local<v8::Context> context = isolate->GetCurrentContext();
      ContextState* state = GetState(context);
      v8::Local<v8::Object> reader = v8::Object::New(isolate);
      (void)reader->Set(context, ToV8Str(isolate, "__stream"), info.This())
          .FromMaybe(false);
      v8::Local<v8::FunctionTemplate> read_tpl = v8::FunctionTemplate::New(
          isolate,
          [](const v8::FunctionCallbackInfo<v8::Value>& args) {
            v8::Isolate* iso = args.GetIsolate();
            v8::Local<v8::Context> ctx = iso->GetCurrentContext();
            v8::Local<v8::Value> stream_value;
            if (!args.This()
                      ->Get(ctx, ToV8Str(iso, "__stream"))
                      .ToLocal(&stream_value) ||
                !stream_value->IsObject())
              return;
            v8::Local<v8::Object> stream = stream_value.As<v8::Object>();
            v8::Local<v8::Promise::Resolver> resolver =
                v8::Promise::Resolver::New(ctx).ToLocalChecked();
            v8::Local<v8::Value> chunks;
            if (stream->GetPrivate(ctx, RsChunksKey(iso)).ToLocal(&chunks) &&
                chunks->IsArray() &&
                chunks.As<v8::Array>()->Length() > 0)
            {
              v8::Local<v8::Array> list = chunks.As<v8::Array>();
              v8::Local<v8::Value> value;
              if (list->Get(ctx, 0).ToLocal(&value))
              {
                // shift: drop the first entry and close the gap
                for (uint32_t k = 0; k + 1 < list->Length(); k++)
                {
                  v8::Local<v8::Value> next;
                  if (list->Get(ctx, k + 1).ToLocal(&next))
                    (void)list->Set(ctx, k, next).FromMaybe(false);
                }
                (void)list->Delete(ctx, list->Length() - 1).FromMaybe(false);
                ResolveValueDone(iso, ctx, resolver, value, false);
              }
              else
              {
                ResolveValueDone(iso, ctx, resolver,
                                 v8::Undefined(iso), true);
              }
            }
            else
            {
              v8::Local<v8::Value> errored;
              bool has_errored = false;
              if (stream->GetPrivate(ctx, RsHasErroredKey(iso))
                      .ToLocal(&errored) &&
                  errored->IsBoolean())
                has_errored = errored->BooleanValue(iso);
              if (has_errored)
              {
                v8::Local<v8::Value> reason;
                if (stream->GetPrivate(ctx, RsErroredKey(iso)).ToLocal(&reason))
                  (void)resolver->Reject(ctx, reason).FromMaybe(false);
                else
                  (void)resolver->Reject(ctx, v8::Null(iso)).FromMaybe(false);
              }
              else
              {
                ResolveValueDone(iso, ctx, resolver, v8::Undefined(iso), true);
              }
            }
            info.GetReturnValue().Set(resolver->GetPromise());
          });
      read_tpl->SetClassName(ToV8Str(isolate, "read"));
      v8::Local<v8::Function> read_fn =
          read_tpl->GetFunction(context).ToLocalChecked();
      (void)reader->Set(context, ToV8Str(isolate, "read"), read_fn)
          .FromMaybe(false);
      v8::Local<v8::FunctionTemplate> cancel_tpl =
          v8::FunctionTemplate::New(isolate, ReadableStreamCancel);
      cancel_tpl->SetClassName(ToV8Str(isolate, "cancel"));
      (void)reader->Set(context, ToV8Str(isolate, "cancel"),
                        cancel_tpl->GetFunction(context).ToLocalChecked())
          .FromMaybe(false);
      v8::Local<v8::FunctionTemplate> release_tpl = v8::FunctionTemplate::New(
          isolate,
          [](const v8::FunctionCallbackInfo<v8::Value>& args) {
            v8::Isolate* iso = args.GetIsolate();
            v8::Local<v8::Context> ctx = iso->GetCurrentContext();
            v8::Local<v8::Value> stream_value;
            if (args.This()
                    ->Get(ctx, ToV8Str(iso, "__stream"))
                    .ToLocal(&stream_value) &&
                stream_value->IsObject())
              (void)stream_value.As<v8::Object>()
                  ->SetPrivate(ctx, RsReaderKey(iso), v8::Null(iso))
                  .FromMaybe(false);
          });
      release_tpl->SetClassName(ToV8Str(isolate, "releaseLock"));
      (void)reader->Set(context, ToV8Str(isolate, "releaseLock"),
                        release_tpl->GetFunction(context).ToLocalChecked())
          .FromMaybe(false);
      (void)info.This()->SetPrivate(context, RsReaderKey(isolate), reader)
          .FromMaybe(false);
      info.GetReturnValue().Set(reader);
    }

    void ReadableStreamLocked(const v8::FunctionCallbackInfo<v8::Value>& info)
    {
      v8::Isolate* isolate = info.GetIsolate();
      v8::Local<v8::Context> context = isolate->GetCurrentContext();
      v8::Local<v8::Value> reader;
      bool locked = info.This()
                        ->GetPrivate(context, RsReaderKey(isolate))
                        .ToLocal(&reader) &&
                    !reader->IsNull() && !reader->IsUndefined();
      info.GetReturnValue().Set(v8::Boolean::New(isolate, locked));
    }

    void ReadableStreamConstructor(
        const v8::FunctionCallbackInfo<v8::Value>& info)
    {
      v8::Isolate* isolate = info.GetIsolate();
      v8::Local<v8::Context> context = isolate->GetCurrentContext();
      ContextState* state = GetState(context);
      if (!info.IsConstructCall() || state == nullptr ||
          state->readable_tpl.IsEmpty())
      {
        isolate->ThrowException(ToV8Str(
            isolate, "TypeError: ReadableStream constructor requires new"));
        return;
      }
      v8::Local<v8::Object> inst = state->readable_tpl.Get(isolate)
                                       ->InstanceTemplate()
                                       ->NewInstance(context)
                                       .ToLocalChecked();
      (void)inst->SetPrivate(context, RsChunksKey(isolate),
                             v8::Array::New(isolate))
          .FromMaybe(false);
      RsSetClosed(isolate, context, inst, false);
      (void)inst->SetPrivate(context, RsErroredKey(isolate), v8::Null(isolate))
          .FromMaybe(false);
      (void)inst->SetPrivate(context, RsHasErroredKey(isolate),
                             v8::Boolean::New(isolate, false))
          .FromMaybe(false);
      v8::Local<v8::Object> underlying =
          info.Length() >= 1 && info[0]->IsObject()
              ? info[0].As<v8::Object>()
              : v8::Object::New(isolate);
      (void)inst->SetPrivate(context, ToV8Str(isolate, "__unused"),
                             v8::Undefined(isolate))
          .FromMaybe(false);
      v8::Local<v8::Object> controller =
          state->readablectl_tpl.Get(isolate)
              ->InstanceTemplate()
              ->NewInstance(context)
              .ToLocalChecked();
      (void)controller->SetPrivate(context, RscStreamKey(isolate), inst)
          .FromMaybe(false);
      (void)inst->SetPrivate(context, RsControllerKey(isolate), controller)
          .FromMaybe(false);
      v8::Local<v8::Value> start;
      if (underlying->Get(context, ToV8Str(isolate, "start")).ToLocal(&start) &&
          start->IsFunction())
      {
        v8::Local<v8::Value> argv[] = {controller};
        v8::TryCatch try_catch(isolate);
        (void)start.As<v8::Function>()
            ->Call(context, underlying, 1, argv)
            .FromMaybe(v8::Local<v8::Value>());
        try_catch.Reset();
      }
      info.GetReturnValue().Set(inst);
    }

    void WritableStreamConstructor(
        const v8::FunctionCallbackInfo<v8::Value>& info)
    {
      v8::Isolate* isolate = info.GetIsolate();
      v8::Local<v8::Context> context = isolate->GetCurrentContext();
      ContextState* state = GetState(context);
      if (!info.IsConstructCall() || state == nullptr ||
          state->writable_tpl.IsEmpty())
      {
        isolate->ThrowException(ToV8Str(
            isolate, "TypeError: WritableStream constructor requires new"));
        return;
      }
      v8::Local<v8::Object> inst = state->writable_tpl.Get(isolate)
                                       ->InstanceTemplate()
                                       ->NewInstance(context)
                                       .ToLocalChecked();
      v8::Local<v8::Object> underlying =
          info.Length() >= 1 && info[0]->IsObject()
              ? info[0].As<v8::Object>()
              : v8::Object::New(isolate);
      (void)inst->SetPrivate(context, WsUnderlyingKey(isolate), underlying)
          .FromMaybe(false);
      info.GetReturnValue().Set(inst);
    }

    void WritableStreamGetWriter(
        const v8::FunctionCallbackInfo<v8::Value>& info)
    {
      v8::Isolate* isolate = info.GetIsolate();
      v8::Local<v8::Context> context = isolate->GetCurrentContext();
      v8::Local<v8::Object> writer = v8::Object::New(isolate);
      (void)writer->Set(context, ToV8Str(isolate, "__underlying"),
                        info.This())
          .FromMaybe(false);
      v8::Local<v8::FunctionTemplate> write_tpl = v8::FunctionTemplate::New(
          isolate,
          [](const v8::FunctionCallbackInfo<v8::Value>& args) {
            v8::Isolate* iso = args.GetIsolate();
            v8::Local<v8::Context> ctx = iso->GetCurrentContext();
            v8::Local<v8::Promise::Resolver> resolver =
                v8::Promise::Resolver::New(ctx).ToLocalChecked();
            v8::Local<v8::Value> underlying;
            if (!args.This()
                      ->Get(ctx, ToV8Str(iso, "__underlying"))
                      .ToLocal(&underlying) ||
                !underlying->IsObject())
            {
              (void)resolver->Resolve(ctx, v8::Undefined(iso))
                  .FromMaybe(false);
              info.GetReturnValue().Set(resolver->GetPromise());
              return;
            }
            v8::Local<v8::Value> sink_fn;
            if (underlying.As<v8::Object>()
                    ->Get(ctx, ToV8Str(iso, "write"))
                    .ToLocal(&sink_fn) &&
                sink_fn->IsFunction())
            {
              v8::Local<v8::Value> sink_argv[] = {
                  args.Length() >= 1 ? args[0] : v8::Undefined(iso)};
              v8::TryCatch try_catch(iso);
              (void)sink_fn.As<v8::Function>()
                  ->Call(ctx, underlying, 1, sink_argv)
                  .FromMaybe(v8::Local<v8::Value>());
              try_catch.Reset();
            }
            (void)resolver->Resolve(ctx, v8::Undefined(iso)).FromMaybe(false);
            info.GetReturnValue().Set(resolver->GetPromise());
          });
      write_tpl->SetClassName(ToV8Str(isolate, "write"));
      v8::Local<v8::FunctionTemplate> close_tpl = v8::FunctionTemplate::New(
          isolate,
          [](const v8::FunctionCallbackInfo<v8::Value>& args) {
            v8::Isolate* iso = args.GetIsolate();
            v8::Local<v8::Context> ctx = iso->GetCurrentContext();
            v8::Local<v8::Promise::Resolver> resolver =
                v8::Promise::Resolver::New(ctx).ToLocalChecked();
            v8::Local<v8::Value> underlying;
            if (args.This()
                    ->Get(ctx, ToV8Str(iso, "__underlying"))
                    .ToLocal(&underlying) &&
                underlying->IsObject())
            {
              v8::Local<v8::Value> sink_fn;
              if (underlying.As<v8::Object>()
                      ->Get(ctx, ToV8Str(iso, "close"))
                      .ToLocal(&sink_fn) &&
                  sink_fn->IsFunction())
              {
                v8::TryCatch try_catch(iso);
                (void)sink_fn.As<v8::Function>()
                    ->Call(ctx, underlying, 0, nullptr)
                    .FromMaybe(v8::Local<v8::Value>());
                try_catch.Reset();
              }
            }
            (void)resolver->Resolve(ctx, v8::Undefined(iso)).FromMaybe(false);
            info.GetReturnValue().Set(resolver->GetPromise());
          });
      close_tpl->SetClassName(ToV8Str(isolate, "close"));
      v8::Local<v8::FunctionTemplate> abort_tpl = v8::FunctionTemplate::New(
          isolate,
          [](const v8::FunctionCallbackInfo<v8::Value>& args) {
            v8::Local<v8::Promise::Resolver> resolver =
                v8::Promise::Resolver::New(
                    args.GetIsolate()->GetCurrentContext())
                    .ToLocalChecked();
            (void)resolver
                ->Resolve(args.GetIsolate()->GetCurrentContext(),
                          v8::Undefined(args.GetIsolate()))
                .FromMaybe(false);
            info.GetReturnValue().Set(resolver->GetPromise());
          });
      abort_tpl->SetClassName(ToV8Str(isolate, "abort"));
      v8::Local<v8::FunctionTemplate> release_tpl = v8::FunctionTemplate::New(
          isolate, [](const v8::FunctionCallbackInfo<v8::Value>&) {});
      release_tpl->SetClassName(ToV8Str(isolate, "releaseLock"));
      (void)writer->Set(context, ToV8Str(isolate, "write"),
                        write_tpl->GetFunction(context).ToLocalChecked())
          .FromMaybe(false);
      (void)writer->Set(context, ToV8Str(isolate, "close"),
                        close_tpl->GetFunction(context).ToLocalChecked())
          .FromMaybe(false);
      (void)writer->Set(context, ToV8Str(isolate, "abort"),
                        abort_tpl->GetFunction(context).ToLocalChecked())
          .FromMaybe(false);
      (void)writer->Set(context, ToV8Str(isolate, "releaseLock"),
                        release_tpl->GetFunction(context).ToLocalChecked())
          .FromMaybe(false);
      info.GetReturnValue().Set(writer);
    }

    void TransformStreamConstructor(
        const v8::FunctionCallbackInfo<v8::Value>& info)
    {
      v8::Isolate* isolate = info.GetIsolate();
      v8::Local<v8::Context> context = isolate->GetCurrentContext();
      ContextState* state = GetState(context);
      if (!info.IsConstructCall() || state == nullptr ||
          state->transform_tpl.IsEmpty() ||
          state->readable_tpl.IsEmpty() ||
          state->writable_tpl.IsEmpty())
      {
        isolate->ThrowException(ToV8Str(
            isolate, "TypeError: TransformStream constructor requires new"));
        return;
      }
      v8::Local<v8::Object> transformer =
          info.Length() >= 1 && info[0]->IsObject()
              ? info[0].As<v8::Object>()
              : v8::Object::New(isolate);
      // readable (its controller is captured below for enqueueing)
      v8::Local<v8::Object> readable = state->readable_tpl.Get(isolate)
                                           ->InstanceTemplate()
                                           ->NewInstance(context)
                                           .ToLocalChecked();
      (void)readable->SetPrivate(context, RsChunksKey(isolate),
                                 v8::Array::New(isolate))
          .FromMaybe(false);
      RsSetClosed(isolate, context, readable, false);
      (void)readable->SetPrivate(context, RsErroredKey(isolate),
                                 v8::Null(isolate))
          .FromMaybe(false);
      (void)readable->SetPrivate(context, RsHasErroredKey(isolate),
                                 v8::Boolean::New(isolate, false))
          .FromMaybe(false);
      v8::Local<v8::Object> controller =
          state->readablectl_tpl.Get(isolate)
              ->InstanceTemplate()
              ->NewInstance(context)
              .ToLocalChecked();
      (void)controller->SetPrivate(context, RscStreamKey(isolate), readable)
          .FromMaybe(false);
      (void)readable->SetPrivate(context, RsControllerKey(isolate), controller)
          .FromMaybe(false);
      // writable whose write() runs the transformer and enqueues into the
      // readable controller
      v8::Local<v8::Object> writable = state->writable_tpl.Get(isolate)
                                           ->InstanceTemplate()
                                           ->NewInstance(context)
                                           .ToLocalChecked();
      v8::Local<v8::Object> underlying = v8::Object::New(isolate);
      v8::Local<v8::Array> transform_data = v8::Array::New(isolate, 2);
      (void)transform_data->Set(context, 0, transformer).FromMaybe(false);
      (void)transform_data->Set(context, 1, controller).FromMaybe(false);
      v8::Local<v8::FunctionTemplate> write_fn = v8::FunctionTemplate::New(
          isolate,
          [](const v8::FunctionCallbackInfo<v8::Value>& args) {
            v8::Isolate* iso = args.GetIsolate();
            v8::Local<v8::Context> ctx = iso->GetCurrentContext();
            v8::Local<v8::Array> data = args.Data().As<v8::Array>();
            v8::Local<v8::Value> transformer, controller;
            if (!data->Get(ctx, 0).ToLocal(&transformer) ||
                !transformer->IsObject() ||
                !data->Get(ctx, 1).ToLocal(&controller) ||
                !controller->IsObject())
              return;
            v8::Local<v8::Value> chunk =
                args.Length() >= 1 ? args[0] : v8::Undefined(iso);
            v8::Local<v8::Value> transform_fn;
            if (transformer.As<v8::Object>()
                    ->Get(ctx, ToV8Str(iso, "transform"))
                    .ToLocal(&transform_fn) &&
                transform_fn->IsFunction())
            {
              v8::Local<v8::Array> outgoing = v8::Array::New(iso);
              v8::Local<v8::Object> downstream = v8::Object::New(iso);
              v8::Local<v8::FunctionTemplate> enqueue_fn =
                  v8::FunctionTemplate::New(
                      iso,
                      [](const v8::FunctionCallbackInfo<v8::Value>& eargs) {
                        v8::Isolate* eiso = eargs.GetIsolate();
                        v8::Local<v8::Context> ectx =
                            eiso->GetCurrentContext();
                        v8::Local<v8::Array> out =
                            eargs.Data().As<v8::Array>();
                        v8::Local<v8::Value> value =
                            eargs.Length() >= 1
                                ? eargs[0]
                                : v8::Local<v8::Value>(v8::Undefined(eiso));
                        (void)out->Set(ectx, out->Length(), value)
                            .FromMaybe(false);
                      },
                      outgoing);
              enqueue_fn->SetClassName(ToV8Str(iso, "enqueue"));
              (void)downstream
                  ->Set(ctx, ToV8Str(iso, "enqueue"),
                        enqueue_fn->GetFunction(ctx).ToLocalChecked())
                  .FromMaybe(false);
              v8::Local<v8::FunctionTemplate> close_fn =
                  v8::FunctionTemplate::New(
                      iso, [](const v8::FunctionCallbackInfo<v8::Value>&) {});
              close_fn->SetClassName(ToV8Str(iso, "close"));
              (void)downstream
                  ->Set(ctx, ToV8Str(iso, "close"),
                        close_fn->GetFunction(ctx).ToLocalChecked())
                  .FromMaybe(false);
              v8::Local<v8::FunctionTemplate> error_fn =
                  v8::FunctionTemplate::New(
                      iso, [](const v8::FunctionCallbackInfo<v8::Value>&) {});
              error_fn->SetClassName(ToV8Str(iso, "error"));
              (void)downstream
                  ->Set(ctx, ToV8Str(iso, "error"),
                        error_fn->GetFunction(ctx).ToLocalChecked())
                  .FromMaybe(false);
              v8::Local<v8::Value> argv[] = {chunk, downstream};
              v8::TryCatch try_catch(iso);
              (void)transform_fn.As<v8::Function>()
                  ->Call(ctx, transformer, 2, argv)
                  .FromMaybe(v8::Local<v8::Value>());
              try_catch.Reset();
              for (uint32_t i = 0; i < outgoing->Length(); i++)
              {
                v8::Local<v8::Value> value;
                if (outgoing->Get(ctx, i).ToLocal(&value))
                {
                  v8::Local<v8::Value> enqueue;
                  if (controller.As<v8::Object>()
                          ->Get(ctx, ToV8Str(iso, "enqueue"))
                          .ToLocal(&enqueue) &&
                      enqueue->IsFunction())
                  {
                    v8::Local<v8::Value> eargv[] = {value};
                    v8::TryCatch try_catch2(iso);
                    (void)enqueue.As<v8::Function>()
                        ->Call(ctx, controller, 1, eargv)
                        .FromMaybe(v8::Local<v8::Value>());
                    try_catch2.Reset();
                  }
                }
              }
            }
          },
          transform_data);
      write_fn->SetClassName(ToV8Str(isolate, "write"));
      (void)underlying
          ->Set(context, ToV8Str(isolate, "write"),
                write_fn->GetFunction(context).ToLocalChecked())
          .FromMaybe(false);
      (void)writable->SetPrivate(context, WsUnderlyingKey(isolate),
                                 underlying)
          .FromMaybe(false);
      (void)inst->Set(context, ToV8Str(isolate, "readable"), readable)
          .FromMaybe(false);
      (void)inst->Set(context, ToV8Str(isolate, "writable"), writable)
          .FromMaybe(false);
    }

    void InstallStreams(v8::Isolate* isolate, v8::Local<v8::Context> context,
                        ContextState* state)
    {
      v8::Local<v8::Object> global = context->Global();
      auto register_global = [&](const char* name,
                                 v8::Local<v8::FunctionTemplate> ctor) {
        (void)global
            ->DefineOwnProperty(context, ToV8Str(isolate, name),
                                ctor->GetFunction(context).ToLocalChecked(),
                                static_cast<v8::PropertyAttribute>(
                                    v8::PropertyAttribute::DontDelete |
                                    v8::PropertyAttribute::DontEnum))
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
      auto getter = [&](v8::Local<v8::FunctionTemplate> target,
                        const char* name, v8::FunctionCallback cb) {
        v8::Local<v8::FunctionTemplate> fn =
            v8::FunctionTemplate::New(isolate, cb);
        fn->SetClassName(ToV8Str(isolate, std::string("get ") + name));
        target->PrototypeTemplate()->SetAccessorProperty(
            ToV8Str(isolate, name), fn, v8::Local<v8::FunctionTemplate>());
      };

      if (state->readablectl_tpl.IsEmpty())
      {
        v8::Local<v8::FunctionTemplate> ctor = v8::FunctionTemplate::New(
            isolate, [](const v8::FunctionCallbackInfo<v8::Value>&) {});
        ctor->SetClassName(ToV8Str(isolate, "ReadableStreamDefaultController"));
        v8::Local<v8::ObjectTemplate> inst = ctor->InstanceTemplate();
        inst->SetInternalFieldCount(0);
        inst->Set(v8::Symbol::GetToStringTag(isolate),
                  ToV8Str(isolate, "ReadableStreamDefaultController"),
                  v8::PropertyAttribute::ReadOnly);
        method(ctor, "enqueue", RsControllerEnqueue);
        method(ctor, "close", RsControllerClose);
        method(ctor, "error", RsControllerError);
        state->readablectl_tpl.Reset(isolate, ctor);
        // the reference does not export the controller as a global
      }

      if (state->readable_tpl.IsEmpty())
      {
        v8::Local<v8::FunctionTemplate> ctor = v8::FunctionTemplate::New(
            isolate, ReadableStreamConstructor);
        ctor->SetClassName(ToV8Str(isolate, "ReadableStream"));
        v8::Local<v8::ObjectTemplate> inst = ctor->InstanceTemplate();
        inst->SetInternalFieldCount(0);
        inst->Set(v8::Symbol::GetToStringTag(isolate),
                  ToV8Str(isolate, "ReadableStream"),
                  v8::PropertyAttribute::ReadOnly);
        getter(ctor, "locked", ReadableStreamLocked);
        method(ctor, "getReader", ReadableStreamGetReader);
        method(ctor, "cancel", ReadableStreamCancel);
        state->readable_tpl.Reset(isolate, ctor);
        register_global("ReadableStream", ctor);
      }

      if (state->writable_tpl.IsEmpty())
      {
        v8::Local<v8::FunctionTemplate> ctor = v8::FunctionTemplate::New(
            isolate,
            [](const v8::FunctionCallbackInfo<v8::Value>& info) {
              v8::Isolate* iso = info.GetIsolate();
              v8::Local<v8::Context> ctx = iso->GetCurrentContext();
              ContextState* st = GetState(ctx);
              if (!info.IsConstructCall() || st == nullptr ||
                  st->writable_tpl.IsEmpty())
              {
                iso->ThrowException(ToV8Str(
                    iso,
                    "TypeError: WritableStream constructor requires new"));
                return;
              }
              v8::Local<v8::Object> inst = st->writable_tpl.Get(iso)
                                               ->InstanceTemplate()
                                               ->NewInstance(ctx)
                                               .ToLocalChecked();
              v8::Local<v8::Object> underlying =
                  info.Length() >= 1 && info[0]->IsObject()
                      ? info[0].As<v8::Object>()
                      : v8::Object::New(iso);
              (void)inst->SetPrivate(ctx, WsUnderlyingKey(iso), underlying)
                  .FromMaybe(false);
              info.GetReturnValue().Set(inst);
            });
        ctor->SetClassName(ToV8Str(isolate, "WritableStream"));
        v8::Local<v8::ObjectTemplate> inst = ctor->InstanceTemplate();
        inst->SetInternalFieldCount(0);
        inst->Set(v8::Symbol::GetToStringTag(isolate),
                  ToV8Str(isolate, "WritableStream"),
                  v8::PropertyAttribute::ReadOnly);
        v8::Local<v8::FunctionTemplate> get_writer =
            v8::FunctionTemplate::New(isolate, WritableStreamGetWriter);
        get_writer->SetClassName(ToV8Str(isolate, "getWriter"));
        ctor->PrototypeTemplate()->Set(ToV8Str(isolate, "getWriter"),
                                       get_writer);
        state->writable_tpl.Reset(isolate, ctor);
        register_global("WritableStream", ctor);
      }

      if (state->transform_tpl.IsEmpty())
      {
        v8::Local<v8::FunctionTemplate> ctor = v8::FunctionTemplate::New(
            isolate, TransformStreamConstructor);
        ctor->SetClassName(ToV8Str(isolate, "TransformStream"));
        v8::Local<v8::ObjectTemplate> inst = ctor->InstanceTemplate();
        inst->SetInternalFieldCount(0);
        inst->Set(v8::Symbol::GetToStringTag(isolate),
                  ToV8Str(isolate, "TransformStream"),
                  v8::PropertyAttribute::ReadOnly);
        state->transform_tpl.Reset(isolate, ctor);
        register_global("TransformStream", ctor);
      }
    }
