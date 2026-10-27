#include "net/rtc_channel.hpp"
#include <emscripten.h>
// clang-format off
EM_JS(int,rr_rtc_new,(),{
    Module.rrPeers ||= new Map();Module.rrPeerNext=(Module.rrPeerNext||0)+1;
    const id=Module.rrPeerNext,s={state:0,packets:[],signals:[],pending:[],local:false,chain:Promise.resolve()};
    Module.rrPeers.set(id,s);
    try {
        s.pc=new RTCPeerConnection({iceServers:[]});
        s.emit=message=>{if(s.signals.length>=64){s.state=3;s.pc.close();}else s.signals.push(message);};
        s.bind=dc=>{
            if(s.dc||dc.label!=='race'||dc.ordered||dc.maxRetransmits!==0){s.state=3;dc.close();return;}
            s.dc=dc;dc.binaryType='arraybuffer';
            dc.onopen=()=>{s.state=1;};dc.onclose=()=>{s.state=2;};dc.onerror=()=>{s.state=3;};
            dc.onmessage=e=>{
                if(!(e.data instanceof ArrayBuffer)||!e.data.byteLength||e.data.byteLength>65536||s.packets.length>=4096){s.state=3;dc.close();return;}
                s.packets.push(new Uint8Array(e.data));
            };
        };
        s.pc.ondatachannel=e=>s.bind(e.channel);
        s.pc.onicecandidate=e=>{if(e.candidate){const m={kind:2,text:e.candidate.candidate,mid:e.candidate.sdpMid||'0'};if(s.local)s.emit(m);else s.pending.push(m);}};
        s.pc.onconnectionstatechange=()=>{if(['failed','closed','disconnected'].includes(s.pc.connectionState))s.state=3;};
        s.describe=async d=>{await s.pc.setLocalDescription(d);s.emit({kind:d.type==='offer'?0:1,text:s.pc.localDescription.sdp});s.local=true;for(const m of s.pending)s.emit(m);s.pending=[];};
    } catch(e){s.state=3;}
    return id;
});
EM_JS(void,rr_rtc_offer,(int id),{const s=Module.rrPeers.get(id);s.chain=s.chain.then(async()=>{s.bind(s.pc.createDataChannel('race',{ordered:false,maxRetransmits:0}));await s.describe(await s.pc.createOffer());}).catch(()=>{s.state=3;});});
EM_JS(void,rr_rtc_signal,(int id,int kind,const char* text,const char* mid),{
    const s=Module.rrPeers.get(id),value=UTF8ToString(text),label=UTF8ToString(mid);
    s.chain=s.chain.then(async()=>{
        if(kind===2)await s.pc.addIceCandidate({candidate:value,sdpMid:label});
        else {await s.pc.setRemoteDescription({type:kind===0?'offer':'answer',sdp:value});if(kind===0)await s.describe(await s.pc.createAnswer());}
    }).catch(()=>{s.state=3;});
});
EM_JS(int,rr_rtc_state,(int id),{return Module.rrPeers.get(id)?.state??2;});
EM_JS(int,rr_rtc_kind,(int id),{return Module.rrPeers.get(id)?.signals[0]?.kind??-1;});
EM_JS(int,rr_rtc_field_size,(int id,int mid),{const m=Module.rrPeers.get(id)?.signals[0];return lengthBytesUTF8((mid?m?.mid:m?.text)||'0')+1;});
EM_JS(void,rr_rtc_field,(int id,int mid,char* out,int size),{const m=Module.rrPeers.get(id)?.signals[0];stringToUTF8((mid?m?.mid:m?.text)||'0',out,size);});
EM_JS(void,rr_rtc_shift,(int id),{Module.rrPeers.get(id)?.signals.shift();});
EM_JS(void,rr_rtc_send,(int id,const uint8_t* bytes,int size),{const s=Module.rrPeers.get(id);try{if(s?.state===1){if(s.dc.bufferedAmount>1048576){s.state=3;s.pc.close();}else s.dc.send(HEAPU8.slice(bytes,bytes+size));}}catch(e){s.state=3;}});
EM_JS(int,rr_rtc_size,(int id),{return Module.rrPeers.get(id)?.packets[0]?.length??-1;});
EM_JS(void,rr_rtc_read,(int id,uint8_t* bytes),{const s=Module.rrPeers.get(id);if(s?.packets.length)HEAPU8.set(s.packets.shift(),bytes);});
EM_JS(void,rr_rtc_close,(int id),{const s=Module.rrPeers.get(id);if(s){s.state=2;s.dc?.close();s.pc?.close();Module.rrPeers.delete(id);}});
// clang-format on
namespace rr::net {
namespace {
class WebPeer final : public RtcPeer {
  public:
    WebPeer() : id_(rr_rtc_new()) {}
    ~WebPeer() override { close(); }
    void offer() override { rr_rtc_offer(id_); }
    void signal(const Message& m) override {
        if (auto o = std::get_if<RtcOffer>(&m))
            rr_rtc_signal(id_, 0, o->sdp.c_str(), "0");
        else if (auto a = std::get_if<RtcAnswer>(&m))
            rr_rtc_signal(id_, 1, a->sdp.c_str(), "0");
        else if (auto c = std::get_if<RtcCandidate>(&m))
            rr_rtc_signal(id_, 2, c->candidate.c_str(), c->mid.c_str());
    }
    std::optional<Message> poll_signal() override {
        const int kind = rr_rtc_kind(id_);
        if (kind < 0) return {};
        auto text = field(0);
        auto mid = field(1);
        rr_rtc_shift(id_);
        if (kind == 0) return RtcOffer{text};
        if (kind == 1) return RtcAnswer{text};
        return RtcCandidate{text, mid};
    }
    ChannelStatus status() const override { return ChannelStatus(rr_rtc_state(id_)); }
    void send(std::span<const uint8_t> bytes) override {
        if (bytes.size() <= max_message_bytes) rr_rtc_send(id_, bytes.data(), int(bytes.size()));
    }
    std::optional<std::vector<uint8_t>> poll() override {
        int size = rr_rtc_size(id_);
        if (size < 0) return {};
        std::vector<uint8_t> bytes(size_t(size), 0);
        rr_rtc_read(id_, bytes.data());
        return bytes;
    }
    void close() override {
        if (id_) {
            rr_rtc_close(id_);
            id_ = 0;
        }
    }

  private:
    std::string field(int mid) {
        const int size = rr_rtc_field_size(id_, mid);
        if (size < 1 || size > int(max_message_bytes)) return {};
        std::string text(size_t(size), 0);
        rr_rtc_field(id_, mid, text.data(), size);
        text.resize(size_t(size - 1));
        return text;
    }
    int id_;
};
} // namespace
std::unique_ptr<RtcPeer> make_rtc_peer(const RtcOptions&) {
    return std::make_unique<WebPeer>();
}
} // namespace rr::net
