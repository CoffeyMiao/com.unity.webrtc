using Newtonsoft.Json;
using System;
using System.Collections;
using Newtonsoft.Json.Linq;
using UnityEngine;
using UnityEngine.Experimental.Rendering;

namespace Unity.WebRTC
{
    internal class Context : IDisposable
    {
        internal IntPtr self;
        internal WeakReferenceTable table;

        private int id;
        private bool disposed;
        private IntPtr renderFunction;
        private IntPtr textureUpdateFunction;

        public static Context Create(int id = 0, EncoderType encoderType = EncoderType.Hardware, bool forTest = false)
        {
            if (encoderType == EncoderType.Hardware && !NativeMethods.GetHardwareEncoderSupport())
            {
                throw new ArgumentException("Hardware encoder is not supported");
            }

            var ptr = NativeMethods.ContextCreate(id, encoderType, forTest);
            return new Context(ptr, id);
        }

        public bool IsNull
        {
            get { return self == IntPtr.Zero; }
        }

        private Context(IntPtr ptr, int id)
        {
            self = ptr;
            this.id = id;
            this.table = new WeakReferenceTable();
        }

        ~Context()
        {
            this.Dispose();
        }

        public void Dispose()
        {
            if (this.disposed)
            {
                return;
            }
            if (self != IntPtr.Zero)
            {
                foreach (var value in table.CopiedValues)
                {
                    if (value == null)
                        continue;
                    var disposable = value as IDisposable;
                    disposable?.Dispose();
                }
                table.Clear();

                NativeMethods.ContextDestroy(id);
                self = IntPtr.Zero;
            }
            this.disposed = true;
            GC.SuppressFinalize(this);
        }

        public void AddRefPtr(IntPtr ptr)
        {
            NativeMethods.ContextAddRefPtr(self, ptr);
        }


        public void DeleteRefPtr(IntPtr ptr)
        {
            NativeMethods.ContextDeleteRefPtr(self, ptr);
        }

        public EncoderType GetEncoderType()
        {
            return NativeMethods.ContextGetEncoderType(self);
        }

        public IntPtr CreatePeerConnection()
        {
            return NativeMethods.ContextCreatePeerConnection(self);
        }

        public IntPtr CreatePeerConnection(string conf)
        {
            return NativeMethods.ContextCreatePeerConnectionWithConfig(self, conf);
        }

        public void DeletePeerConnection(IntPtr ptr)
        {
            NativeMethods.ContextDeletePeerConnection(self, ptr);
        }

        public RTCError PeerConnectionSetLocalDescription(IntPtr ptr, ref RTCSessionDescription desc)
        {
            IntPtr ptrError = IntPtr.Zero;
#if !UNITY_WEBGL
            RTCErrorType errorType = NativeMethods.PeerConnectionSetLocalDescription(
                self, ptr, ref desc, ref ptrError);
            string message = ptrError != IntPtr.Zero ? ptrError.AsAnsiStringWithFreeMem() : null;
            return new RTCError { errorType = errorType, message = message};
#else
            IntPtr buf = NativeMethods.PeerConnectionSetLocalDescription(self, ptr, desc.type, desc.sdp);
            var arr = NativeMethods.ptrToIntPtrArray(buf);
            RTCErrorType errorType = (RTCErrorType)arr[0];
            string errorMsg = arr[1].AsAnsiStringWithFreeMem();
            return new RTCError { errorType = errorType, message = errorMsg };
#endif
        }

        public RTCError PeerConnectionSetLocalDescription(IntPtr ptr)
        {
            IntPtr ptrError = IntPtr.Zero;
#if !UNITY_WEBGL
            RTCErrorType errorType =
                NativeMethods.PeerConnectionSetLocalDescriptionWithoutDescription(self, ptr, ref ptrError);
#else
            RTCErrorType errorType = RTCErrorType.InternalError; // TODO
#endif
            string message = ptrError != IntPtr.Zero ? ptrError.AsAnsiStringWithFreeMem() : null;
            return new RTCError { errorType = errorType, message = message };
        }

        public RTCError PeerConnectionSetRemoteDescription(IntPtr ptr, ref RTCSessionDescription desc)
        {
            IntPtr ptrError = IntPtr.Zero;
#if !UNITY_WEBGL
            RTCErrorType errorType = NativeMethods.PeerConnectionSetRemoteDescription(
                self, ptr, ref desc, ref ptrError);
            string message = ptrError != IntPtr.Zero ? ptrError.AsAnsiStringWithFreeMem() : null;
            return new RTCError { errorType = errorType, message = message};
#else
            IntPtr buf = NativeMethods.PeerConnectionSetRemoteDescription(self, ptr, desc.type, desc.sdp);
            var arr = NativeMethods.ptrToIntPtrArray(buf);
            RTCErrorType errorType = (RTCErrorType)arr[0];
            string errorMsg = arr[1].AsAnsiStringWithFreeMem();
            return new RTCError { errorType = errorType, message = errorMsg };
#endif
        }

        public void PeerConnectionRegisterOnSetSessionDescSuccess(IntPtr ptr, DelegateNativePeerConnectionSetSessionDescSuccess callback)
        {
            NativeMethods.PeerConnectionRegisterOnSetSessionDescSuccess(self, ptr, callback);
        }

        public void PeerConnectionRegisterOnSetSessionDescFailure(IntPtr ptr, DelegateNativePeerConnectionSetSessionDescFailure callback)
        {
            NativeMethods.PeerConnectionRegisterOnSetSessionDescFailure(self, ptr, callback);
        }

        public IntPtr PeerConnectionAddTransceiver(IntPtr pc, IntPtr track)
        {
            return NativeMethods.PeerConnectionAddTransceiver(self, pc, track);
        }

        public IntPtr PeerConnectionAddTransceiverWithType(IntPtr pc, TrackKind kind)
        {
            return NativeMethods.PeerConnectionAddTransceiverWithType(self, pc, kind);
        }

        public IntPtr PeerConnectionGetReceivers(IntPtr ptr, out ulong length)
        {
#if !UNITY_WEBGL
            return NativeMethods.PeerConnectionGetReceivers(self, ptr, out length);
#else
            length = 0;
            return NativeMethods.PeerConnectionGetReceivers(self, ptr);
#endif
        }

        public IntPtr PeerConnectionGetSenders(IntPtr ptr, out ulong length)
        {
#if !UNITY_WEBGL
            return NativeMethods.PeerConnectionGetSenders(self, ptr, out length);
#else
            length = 0;
            return NativeMethods.PeerConnectionGetSenders(self, ptr);
#endif
        }

        public IntPtr PeerConnectionGetTransceivers(IntPtr ptr, out ulong length)
        {
#if !UNITY_WEBGL
            return NativeMethods.PeerConnectionGetTransceivers(self, ptr, out length);
#else
            length = 0;
            return NativeMethods.PeerConnectionGetTransceivers(self, ptr);
#endif
        }

        public IntPtr CreateDataChannel(IntPtr ptr, string label, ref RTCDataChannelInitInternal options)
        {
#if !UNITY_WEBGL
            return NativeMethods.ContextCreateDataChannel(self, ptr, label, ref options);
#else
            var optionsJson = JsonUtility.ToJson(options);
            return NativeMethods.ContextCreateDataChannel(self, ptr, label, optionsJson);
#endif
        }

        public void DeleteDataChannel(IntPtr ptr)
        {
            NativeMethods.ContextDeleteDataChannel(self, ptr);
        }

        public IntPtr CreateMediaStream(string label)
        {
            return NativeMethods.ContextCreateMediaStream(self, label);
        }

        public void RegisterMediaStreamObserver(MediaStream stream)
        {
            NativeMethods.ContextRegisterMediaStreamObserver(self, stream.GetSelfOrThrow());
        }

        public void UnRegisterMediaStreamObserver(MediaStream stream)
        {
            NativeMethods.ContextRegisterMediaStreamObserver(self, stream.GetSelfOrThrow());
        }

        public void MediaStreamRegisterOnAddTrack(MediaStream stream, DelegateNativeMediaStreamOnAddTrack callback)
        {
            NativeMethods.MediaStreamRegisterOnAddTrack(self, stream.GetSelfOrThrow(), callback);
        }

        public void MediaStreamRegisterOnRemoveTrack(MediaStream stream, DelegateNativeMediaStreamOnRemoveTrack callback)
        {
            NativeMethods.MediaStreamRegisterOnRemoveTrack(self, stream.GetSelfOrThrow(), callback);
        }


        public void AudioTrackRegisterAudioReceiveCallback(IntPtr track, DelegateAudioReceive callback)
        {
            NativeMethods.ContextRegisterAudioReceiveCallback(self, track, callback);
        }

        public void AudioTrackUnregisterAudioReceiveCallback(IntPtr track)
        {
            NativeMethods.ContextUnregisterAudioReceiveCallback(self, track);
        }

#if !UNITY_WEBGL
        public IntPtr GetRenderEventFunc()
        {
            return NativeMethods.GetRenderEventFunc(self);
        }

        public IntPtr GetUpdateTextureFunc()
        {
            return NativeMethods.GetUpdateTextureFunc(self);
        }
#endif

        public IntPtr CreateVideoTrackSource()
        {
            return NativeMethods.ContextCreateVideoTrackSource(self);
        }

        public IntPtr CreateAudioTrackSource()
        {
            return NativeMethods.ContextCreateAudioTrackSource(self);
        }

        public IntPtr CreateAudioTrack(string label, IntPtr trackSource)
        {
            return NativeMethods.ContextCreateAudioTrack(self, label, trackSource);
        }
#if !UNITY_WEBGL
        public IntPtr CreateVideoTrack(string label, IntPtr source)
        {
            return NativeMethods.ContextCreateVideoTrack(self, label, source);
        }
#else
        public IntPtr CreateVideoTrack(string label)
        {
            return IntPtr.Zero; // NativeMethods.ContextCreateVideoTrack(self, label);
        }

        public IntPtr CreateVideoTrack(IntPtr srcTexturePtr, IntPtr dstTexturePtr, int width, int height)
        {
            return NativeMethods.ContextCreateVideoTrack(self, srcTexturePtr, dstTexturePtr, width, height);
        }
#endif

        public void StopMediaStreamTrack(IntPtr track)
        {
            NativeMethods.ContextStopMediaStreamTrack(self, track);
        }


        public IntPtr CreateVideoRenderer()
        {
            return NativeMethods.CreateVideoRenderer(self);
        }

        public void DeleteVideoRenderer(IntPtr sink)
        {
            NativeMethods.DeleteVideoRenderer(self, sink);
        }

        public void DeleteStatsReport(IntPtr report)
        {
            NativeMethods.ContextDeleteStatsReport(self, report);
        }

#if !UNITY_WEBGL
        public void SetVideoEncoderParameter(IntPtr track, int width, int height, GraphicsFormat format, IntPtr texturePtr)
        {
            NativeMethods.ContextSetVideoEncoderParameter(self, track, width, height, format, texturePtr);
        }

        public CodecInitializationResult GetInitializationResult(IntPtr track)
        {
            return NativeMethods.GetInitializationResult(self, track);
        }
#endif

#if !UNITY_WEBGL
        public void GetSenderCapabilities(TrackKind kind, out IntPtr capabilities)
        {
            NativeMethods.ContextGetSenderCapabilities(self, kind, out capabilities);
        }
#else
        public RTCRtpCapabilities GetSenderCapabilities(TrackKind kind)
        {
            string json = NativeMethods.ContextGetSenderCapabilities(self, kind);
            return JsonConvert.DeserializeObject<RTCRtpCapabilities>(json);
        }
#endif

#if !UNITY_WEBGL
        public void GetReceiverCapabilities(TrackKind kind, out IntPtr capabilities)
        {
            NativeMethods.ContextGetReceiverCapabilities(self, kind, out capabilities);
        }
#else
        public RTCRtpCapabilities GetReceiverCapabilities(TrackKind kind)
        {
            string json = NativeMethods.ContextGetReceiverCapabilities(self, kind);
            return JsonConvert.DeserializeObject<RTCRtpCapabilities>(json);
        }
#endif


#if !UNITY_WEBGL
        internal void InitializeEncoder(IntPtr track)
        {
            renderFunction = renderFunction == IntPtr.Zero ? GetRenderEventFunc() : renderFunction;
            VideoEncoderMethods.InitializeEncoder(renderFunction, track);
        }

        internal void FinalizeEncoder(IntPtr track)
        {
            renderFunction = renderFunction == IntPtr.Zero ? GetRenderEventFunc() : renderFunction;
            VideoEncoderMethods.FinalizeEncoder(renderFunction, track);
        }

        internal void Encode(IntPtr track)
        {
            renderFunction = renderFunction == IntPtr.Zero ? GetRenderEventFunc() : renderFunction;
            VideoEncoderMethods.Encode(renderFunction, track);
        }

        internal void UpdateRendererTexture(uint rendererId, UnityEngine.Texture texture)
        {
            textureUpdateFunction = textureUpdateFunction == IntPtr.Zero ? GetUpdateTextureFunc() : textureUpdateFunction;
            VideoDecoderMethods.UpdateRendererTexture(textureUpdateFunction, texture, rendererId);
        }
#endif
    }
}
