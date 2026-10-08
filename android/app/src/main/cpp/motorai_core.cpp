#include "motorai_core.h"

#include <algorithm>
#include <chrono>
#include <cctype>
#include <cmath>
#include <filesystem>
#include <fstream>
#include <functional>
#include <iomanip>
#include <limits>
#include <mutex>
#include <numeric>
#include <random>
#include <sstream>
#include <stdexcept>
#include <unordered_set>
#include <utility>
#include <vector>

namespace motorai {
namespace {

struct Node {
    std::vector<int> shape;
    std::vector<float> data;
    std::vector<float> grad;
    bool requires_grad = false;
    std::vector<std::shared_ptr<Node>> parents;
    std::function<void()> backward = []{};
};

struct Tensor {
    std::shared_ptr<Node> n;
    Tensor() = default;
    explicit Tensor(std::shared_ptr<Node> p) : n(std::move(p)) {}
    int size() const { return static_cast<int>(n->data.size()); }
    int dim(int i) const { return n->shape.at(i); }
};

int numel(const std::vector<int>& s) {
    int n = 1;
    for (int x : s) n *= x;
    return n;
}

float deterministicUnit(std::mt19937& rng) {
    return static_cast<float>((static_cast<double>(rng()) + 0.5) / 4294967296.0);
}

float deterministicNormalApprox(std::mt19937& rng) {
    double sum = 0.0;
    for (int i = 0; i < 12; ++i) sum += deterministicUnit(rng);
    return static_cast<float>(sum - 6.0);
}

size_t deterministicIndex(std::mt19937& rng, size_t count) {
    if (count == 0) throw std::runtime_error("deterministicIndex count=0");
    return static_cast<size_t>((static_cast<uint64_t>(rng()) * static_cast<uint64_t>(count)) >> 32);
}

template <typename T>
void deterministicShuffle(std::vector<T>& values, std::mt19937& rng) {
    if (values.empty()) return;
    for (size_t i = values.size() - 1; i > 0; --i) {
        size_t j = deterministicIndex(rng, i + 1);
        std::swap(values[i], values[j]);
    }
}

Tensor tensor(std::vector<int> shape, std::vector<float> data, bool req=false) {
    if (numel(shape) != static_cast<int>(data.size())) throw std::runtime_error("tensor shape mismatch");
    auto p = std::make_shared<Node>();
    p->shape = std::move(shape);
    p->data = std::move(data);
    p->requires_grad = req;
    if (req) p->grad.assign(p->data.size(), 0.0f);
    return Tensor(p);
}

Tensor zeros(const std::vector<int>& shape, bool req=false) {
    return tensor(shape, std::vector<float>(numel(shape), 0.0f), req);
}

void topoDfs(const std::shared_ptr<Node>& n, std::unordered_set<Node*>& seen, std::vector<std::shared_ptr<Node>>& out) {
    if (!n || seen.count(n.get())) return;
    seen.insert(n.get());
    for (auto& p : n->parents) topoDfs(p, seen, out);
    out.push_back(n);
}

void backward(const Tensor& root) {
    if (root.size() != 1) throw std::runtime_error("backward root must be scalar");
    std::unordered_set<Node*> seen;
    std::vector<std::shared_ptr<Node>> topo;
    topoDfs(root.n, seen, topo);
    if (root.n->grad.empty()) root.n->grad.assign(1, 1.0f); else root.n->grad[0] = 1.0f;
    for (auto it = topo.rbegin(); it != topo.rend(); ++it) (*it)->backward();
}

Tensor add(const Tensor& a, const Tensor& b) {
    if (a.n->shape != b.n->shape) throw std::runtime_error("add shape mismatch");
    std::vector<float> out(a.size());
    for (int i=0;i<a.size();++i) out[i]=a.n->data[i]+b.n->data[i];
    bool req=a.n->requires_grad||b.n->requires_grad;
    Tensor y=tensor(a.n->shape,std::move(out),req);
    y.n->parents={a.n,b.n};
    y.n->backward=[pa=a.n,pb=b.n,py=y.n.get()]{
        if (pa->requires_grad) for(size_t i=0;i<pa->grad.size();++i) pa->grad[i]+=py->grad[i];
        if (pb->requires_grad) for(size_t i=0;i<pb->grad.size();++i) pb->grad[i]+=py->grad[i];
    };
    return y;
}

Tensor addBias(const Tensor& a, const Tensor& b) {
    if (a.n->shape.size()!=2 || b.n->shape.size()!=1 || a.dim(1)!=b.dim(0)) throw std::runtime_error("bias shape mismatch");
    int m=a.dim(0), n=a.dim(1);
    std::vector<float> out(a.size());
    for(int i=0;i<m;++i) for(int j=0;j<n;++j) out[i*n+j]=a.n->data[i*n+j]+b.n->data[j];
    bool req=a.n->requires_grad||b.n->requires_grad;
    Tensor y=tensor({m,n},std::move(out),req);
    y.n->parents={a.n,b.n};
    y.n->backward=[pa=a.n,pb=b.n,py=y.n.get(),m,n]{
        if(pa->requires_grad) for(size_t i=0;i<pa->grad.size();++i) pa->grad[i]+=py->grad[i];
        if(pb->requires_grad) for(int i=0;i<m;++i) for(int j=0;j<n;++j) pb->grad[j]+=py->grad[i*n+j];
    };
    return y;
}

Tensor scale(const Tensor& a,float s) {
    std::vector<float> out(a.size()); for(int i=0;i<a.size();++i) out[i]=a.n->data[i]*s;
    Tensor y=tensor(a.n->shape,std::move(out),a.n->requires_grad);
    y.n->parents={a.n};
    y.n->backward=[pa=a.n,py=y.n.get(),s]{ if(pa->requires_grad) for(size_t i=0;i<pa->grad.size();++i) pa->grad[i]+=py->grad[i]*s; };
    return y;
}

Tensor binaryAnswerOverride(const Tensor& base,const Tensor& binary,const std::vector<int>& ids,int plusId=12,int minusId=13) {
    if(base.n->shape.size()!=2 || binary.n->shape.size()!=2 ||
       base.dim(0)!=binary.dim(0) || binary.dim(1)!=2)
        throw std::runtime_error("binary answer override shape mismatch");
    int rows=base.dim(0), vocab=base.dim(1);
    std::vector<float> out=base.n->data;
    for(int i=0;i<rows && i<(int)ids.size();++i){
        if(ids[i]==1){
            out[i*vocab+0]=-1e9f;
            out[i*vocab+plusId]=binary.n->data[i*2+0];
            out[i*vocab+minusId]=binary.n->data[i*2+1];
        }
    }
    bool req=base.n->requires_grad||binary.n->requires_grad;
    Tensor y=tensor({rows,vocab},std::move(out),req);
    y.n->parents={base.n,binary.n};
    y.n->backward=[pb=base.n,pbin=binary.n,py=y.n.get(),ids,rows,vocab,plusId,minusId]{
        if(pb->requires_grad){
            for(int i=0;i<rows;++i) for(int j=0;j<vocab;++j){
                bool replaced=i<(int)ids.size() && ids[i]==1 && (j==0 || j==plusId || j==minusId);
                if(!replaced) pb->grad[i*vocab+j]+=py->grad[i*vocab+j];
            }
        }
        if(pbin->requires_grad){
            for(int i=0;i<rows && i<(int)ids.size();++i){
                if(ids[i]==1){
                    pbin->grad[i*2+0]+=py->grad[i*vocab+plusId];
                    pbin->grad[i*2+1]+=py->grad[i*vocab+minusId];
                }
            }
        }
    };
    return y;
}

Tensor matmul(const Tensor& a,const Tensor& b) {
    if(a.n->shape.size()!=2||b.n->shape.size()!=2||a.dim(1)!=b.dim(0)) throw std::runtime_error("matmul shape mismatch");
    int m=a.dim(0),k=a.dim(1),n=b.dim(1);
    std::vector<float> out(m*n,0.0f);
    for(int i=0;i<m;++i) for(int p=0;p<k;++p){ float av=a.n->data[i*k+p]; for(int j=0;j<n;++j) out[i*n+j]+=av*b.n->data[p*n+j]; }
    bool req=a.n->requires_grad||b.n->requires_grad;
    Tensor y=tensor({m,n},std::move(out),req);
    y.n->parents={a.n,b.n};
    y.n->backward=[pa=a.n,pb=b.n,py=y.n.get(),m,k,n]{
        if(pa->requires_grad){
            for(int i=0;i<m;++i) for(int p=0;p<k;++p){ float s=0; for(int j=0;j<n;++j) s+=py->grad[i*n+j]*pb->data[p*n+j]; pa->grad[i*k+p]+=s; }
        }
        if(pb->requires_grad){
            for(int p=0;p<k;++p) for(int j=0;j<n;++j){ float s=0; for(int i=0;i<m;++i) s+=pa->data[i*k+p]*py->grad[i*n+j]; pb->grad[p*n+j]+=s; }
        }
    };
    return y;
}

Tensor transpose2(const Tensor& a) {
    if(a.n->shape.size()!=2) throw std::runtime_error("transpose requires 2D");
    int m=a.dim(0),n=a.dim(1); std::vector<float> out(n*m);
    for(int i=0;i<m;++i) for(int j=0;j<n;++j) out[j*m+i]=a.n->data[i*n+j];
    Tensor y=tensor({n,m},std::move(out),a.n->requires_grad); y.n->parents={a.n};
    y.n->backward=[pa=a.n,py=y.n.get(),m,n]{ if(pa->requires_grad) for(int i=0;i<m;++i) for(int j=0;j<n;++j) pa->grad[i*n+j]+=py->grad[j*m+i]; };
    return y;
}

Tensor causalMask(const Tensor& a) {
    if(a.n->shape.size()!=2||a.dim(0)!=a.dim(1)) throw std::runtime_error("causal mask requires square");
    int t=a.dim(0); std::vector<float> out=a.n->data;
    for(int i=0;i<t;++i) for(int j=i+1;j<t;++j) out[i*t+j]-=1e9f;
    Tensor y=tensor({t,t},std::move(out),a.n->requires_grad); y.n->parents={a.n};
    y.n->backward=[pa=a.n,py=y.n.get(),t]{ if(pa->requires_grad) for(int i=0;i<t;++i) for(int j=0;j<=i;++j) pa->grad[i*t+j]+=py->grad[i*t+j]; };
    return y;
}

Tensor softmaxRows(const Tensor& a) {
    if(a.n->shape.size()!=2) throw std::runtime_error("softmax requires 2D");
    int m=a.dim(0),n=a.dim(1); std::vector<float> out(a.size());
    for(int i=0;i<m;++i){
        float mx=-std::numeric_limits<float>::infinity(); for(int j=0;j<n;++j) mx=std::max(mx,a.n->data[i*n+j]);
        float sum=0; for(int j=0;j<n;++j){ out[i*n+j]=std::exp(a.n->data[i*n+j]-mx); sum+=out[i*n+j]; }
        for(int j=0;j<n;++j) out[i*n+j]/=sum;
    }
    Tensor y=tensor({m,n},std::move(out),a.n->requires_grad); y.n->parents={a.n};
    y.n->backward=[pa=a.n,py=y.n.get(),m,n]{
        if(!pa->requires_grad) return;
        for(int i=0;i<m;++i){ float dot=0; for(int j=0;j<n;++j) dot+=py->grad[i*n+j]*py->data[i*n+j]; for(int j=0;j<n;++j) pa->grad[i*n+j]+=py->data[i*n+j]*(py->grad[i*n+j]-dot); }
    };
    return y;
}

Tensor gelu(const Tensor& a) {
    std::vector<float> out(a.size());
    for(int i=0;i<a.size();++i){ float x=a.n->data[i]; float u=0.7978845608f*(x+0.044715f*x*x*x); out[i]=0.5f*x*(1.0f+std::tanh(u)); }
    Tensor y=tensor(a.n->shape,std::move(out),a.n->requires_grad); y.n->parents={a.n};
    y.n->backward=[pa=a.n,py=y.n.get()]{
        if(!pa->requires_grad) return;
        for(size_t i=0;i<pa->data.size();++i){ float x=pa->data[i]; float u=0.7978845608f*(x+0.044715f*x*x*x); float th=std::tanh(u); float du=0.7978845608f*(1.0f+3.0f*0.044715f*x*x); float d=0.5f*(1.0f+th)+0.5f*x*(1.0f-th*th)*du; pa->grad[i]+=py->grad[i]*d; }
    };
    return y;
}

Tensor embedding(const Tensor& w,const std::vector<int>& ids) {
    if(w.n->shape.size()!=2) throw std::runtime_error("embedding weight must be 2D");
    int n=w.dim(0),d=w.dim(1),t=static_cast<int>(ids.size()); std::vector<float> out(t*d);
    for(int i=0;i<t;++i){ if(ids[i]<0||ids[i]>=n) throw std::runtime_error("token out of range"); for(int j=0;j<d;++j) out[i*d+j]=w.n->data[ids[i]*d+j]; }
    Tensor y=tensor({t,d},std::move(out),w.n->requires_grad); y.n->parents={w.n};
    y.n->backward=[pw=w.n,py=y.n.get(),ids,t,d]{ if(!pw->requires_grad)return; for(int i=0;i<t;++i) for(int j=0;j<d;++j) pw->grad[ids[i]*d+j]+=py->grad[i*d+j]; };
    return y;
}

Tensor layerNorm(const Tensor& x,const Tensor& gamma,const Tensor& beta,float eps=1e-5f) {
    if(x.n->shape.size()!=2||gamma.n->shape.size()!=1||beta.n->shape.size()!=1||x.dim(1)!=gamma.dim(0)||gamma.dim(0)!=beta.dim(0)) throw std::runtime_error("layernorm shape mismatch");
    int m=x.dim(0),d=x.dim(1); std::vector<float> out(x.size()), xhat(x.size()), inv(m);
    for(int r=0;r<m;++r){ float mean=0; for(int j=0;j<d;++j) mean+=x.n->data[r*d+j]; mean/=d; float var=0; for(int j=0;j<d;++j){ float c=x.n->data[r*d+j]-mean; var+=c*c; } var/=d; inv[r]=1.0f/std::sqrt(var+eps); for(int j=0;j<d;++j){ float h=(x.n->data[r*d+j]-mean)*inv[r]; xhat[r*d+j]=h; out[r*d+j]=h*gamma.n->data[j]+beta.n->data[j]; } }
    bool req=x.n->requires_grad||gamma.n->requires_grad||beta.n->requires_grad;
    Tensor y=tensor({m,d},std::move(out),req); y.n->parents={x.n,gamma.n,beta.n};
    y.n->backward=[px=x.n,pg=gamma.n,pb=beta.n,py=y.n.get(),xhat=std::move(xhat),inv=std::move(inv),m,d]{
        if(pg->requires_grad) for(int r=0;r<m;++r) for(int j=0;j<d;++j) pg->grad[j]+=py->grad[r*d+j]*xhat[r*d+j];
        if(pb->requires_grad) for(int r=0;r<m;++r) for(int j=0;j<d;++j) pb->grad[j]+=py->grad[r*d+j];
        if(px->requires_grad){
            for(int r=0;r<m;++r){ float sum1=0,sum2=0; for(int j=0;j<d;++j){ float dh=py->grad[r*d+j]*pg->data[j]; sum1+=dh; sum2+=dh*xhat[r*d+j]; }
                for(int j=0;j<d;++j){ float dh=py->grad[r*d+j]*pg->data[j]; px->grad[r*d+j]+=inv[r]/d*(d*dh-sum1-xhat[r*d+j]*sum2); }
            }
        }
    };
    return y;
}

Tensor crossEntropy(const Tensor& logits,const std::vector<int>& targets) {
    if(logits.n->shape.size()!=2||logits.dim(0)!=(int)targets.size()) throw std::runtime_error("cross entropy shape mismatch");
    int t=logits.dim(0),v=logits.dim(1); float loss=0; std::vector<float> probs(logits.size());
    for(int i=0;i<t;++i){ float mx=-std::numeric_limits<float>::infinity(); for(int j=0;j<v;++j) mx=std::max(mx,logits.n->data[i*v+j]); float sum=0; for(int j=0;j<v;++j){ probs[i*v+j]=std::exp(logits.n->data[i*v+j]-mx); sum+=probs[i*v+j]; } for(int j=0;j<v;++j) probs[i*v+j]/=sum; loss-=std::log(std::max(probs[i*v+targets[i]],1e-12f)); }
    loss/=t; Tensor y=tensor({1},{loss},logits.n->requires_grad); y.n->parents={logits.n};
    y.n->backward=[pl=logits.n,py=y.n.get(),probs=std::move(probs),targets,t,v]{ if(!pl->requires_grad)return; float g=py->grad[0]/t; for(int i=0;i<t;++i) for(int j=0;j<v;++j){ float d=probs[i*v+j]-(j==targets[i]?1.0f:0.0f); pl->grad[i*v+j]+=g*d; } };
    return y;
}

Tensor crossEntropyRange(const Tensor& logits,const std::vector<int>& targets,int start,int count) {
    if(logits.n->shape.size()!=2||logits.dim(0)!=(int)targets.size()) throw std::runtime_error("cross entropy range shape mismatch");
    int t=logits.dim(0),v=logits.dim(1);
    start=std::max(0,start); count=std::max(1,count);
    int stop=std::min(t,start+count);
    if(start>=stop) throw std::runtime_error("empty loss range");
    float loss=0; std::vector<float> probs(logits.size(),0.0f);
    for(int i=start;i<stop;++i){
        float mx=-std::numeric_limits<float>::infinity();
        for(int j=0;j<v;++j) mx=std::max(mx,logits.n->data[i*v+j]);
        float sum=0;
        for(int j=0;j<v;++j){ probs[i*v+j]=std::exp(logits.n->data[i*v+j]-mx); sum+=probs[i*v+j]; }
        for(int j=0;j<v;++j) probs[i*v+j]/=sum;
        loss-=std::log(std::max(probs[i*v+targets[i]],1e-12f));
    }
    int npos=stop-start; loss/=npos;
    Tensor y=tensor({1},{loss},logits.n->requires_grad); y.n->parents={logits.n};
    y.n->backward=[pl=logits.n,py=y.n.get(),probs=std::move(probs),targets,start,stop,v,npos]{
        if(!pl->requires_grad)return;
        float g=py->grad[0]/npos;
        for(int i=start;i<stop;++i) for(int j=0;j<v;++j){
            float d=probs[i*v+j]-(j==targets[i]?1.0f:0.0f);
            pl->grad[i*v+j]+=g*d;
        }
    };
    return y;
}

struct Param {
    std::string name;
    Tensor value;
    std::vector<float> m,v;
};

class TinyTransformer {
public:
    int vocab=32, context=7, d=32, ff=64;
    std::vector<Param> p;
    std::mt19937 rng;
    std::mt19937 extra_rng;

    explicit TinyTransformer(uint32_t seed):rng(seed),extra_rng(seed ^ 0xA17E5EEDu){ init(); }

    Tensor& P(const std::string& name){ for(auto& z:p)if(z.name==name)return z.value; throw std::runtime_error("missing param "+name); }
    const Tensor& P(const std::string& name) const { for(auto& z:p)if(z.name==name)return z.value; throw std::runtime_error("missing param "+name); }
    Param& PP(const std::string& name){ for(auto& z:p)if(z.name==name)return z; throw std::runtime_error("missing param "+name); }

    void addParam(const std::string& name,std::vector<int> shape,bool normal=true,float fill=0.0f){
        std::vector<float> data(numel(shape));
        if(normal){ for(float&x:data)x=0.02f*deterministicNormalApprox(rng);} else std::fill(data.begin(),data.end(),fill);
        Tensor t=tensor(shape,std::move(data),true); p.push_back({name,t,std::vector<float>(t.size(),0),std::vector<float>(t.size(),0)});
    }
    void addTokenParam(){
        std::vector<float> data(vocab*d,0.0f);
        // Gli 11 token storici consumano esattamente lo stesso stream RNG della Seed009.
        for(int i=0;i<11*d;++i) data[i]=0.02f*deterministicNormalApprox(rng);
        for(int row=11;row<vocab;++row) for(int j=0;j<d;++j) data[row*d+j]=0.02f*deterministicNormalApprox(extra_rng);
        Tensor t=tensor({vocab,d},std::move(data),true);
        p.push_back({"token",t,std::vector<float>(t.size(),0),std::vector<float>(t.size(),0)});
    }
    void addHeadParam(){
        std::vector<float> data(d*vocab,0.0f);
        for(int i=0;i<d;++i){
            for(int j=0;j<11;++j) data[i*vocab+j]=0.02f*deterministicNormalApprox(rng);
            for(int j=11;j<vocab;++j) data[i*vocab+j]=0.0f; // nuovi output iniziano neutri.
        }
        Tensor t=tensor({d,vocab},std::move(data),true);
        p.push_back({"head.w",t,std::vector<float>(t.size(),0),std::vector<float>(t.size(),0)});
    }
    void init(){
        p.clear(); addTokenParam(); addParam("pos",{context,d});
        addParam("ln1.g",{d},false,1); addParam("ln1.b",{d},false,0);
        addParam("q.w",{d,d}); addParam("q.b",{d},false,0); addParam("k.w",{d,d}); addParam("k.b",{d},false,0); addParam("v.w",{d,d}); addParam("v.b",{d},false,0); addParam("o.w",{d,d}); addParam("o.b",{d},false,0);
        addParam("ln2.g",{d},false,1); addParam("ln2.b",{d},false,0);
        addParam("fc1.w",{d,ff}); addParam("fc1.b",{ff},false,0); addParam("fc2.w",{ff,d}); addParam("fc2.b",{d},false,0);
        addParam("lnf.g",{d},false,1); addParam("lnf.b",{d},false,0); addHeadParam();
        // Growth Adapter 1: added after all legacy params so old random weights remain bit-for-bit stable.
        addParam("grow1.ln.g",{d},false,1); addParam("grow1.ln.b",{d},false,0);
        addParam("grow1.fc1.w",{d,ff}); addParam("grow1.fc1.b",{ff},false,0);
        addParam("grow1.fc2.w",{ff,d},false,0); addParam("grow1.fc2.b",{d},false,0);

        // Growth Adapter 2: attenzione relazionale. O-projection a zero = effetto iniziale esattamente nullo.
        addParam("grow2.ln.g",{d},false,1); addParam("grow2.ln.b",{d},false,0);
        addParam("grow2.q.w",{d,d}); addParam("grow2.q.b",{d},false,0);
        addParam("grow2.k.w",{d,d}); addParam("grow2.k.b",{d},false,0);
        addParam("grow2.v.w",{d,d}); addParam("grow2.v.b",{d},false,0);
        addParam("grow2.o.w",{d,d},false,0); addParam("grow2.o.b",{d},false,0);

        // Growth Adapter 3: attenzione dedicata a L5. Output a zero = nessun effetto sulla Seed010 al caricamento.
        addParam("grow3.ln.g",{d},false,1); addParam("grow3.ln.b",{d},false,0);
        addParam("grow3.q.w",{d,d}); addParam("grow3.q.b",{d},false,0);
        addParam("grow3.k.w",{d,d}); addParam("grow3.k.b",{d},false,0);
        addParam("grow3.v.w",{d,d}); addParam("grow3.v.b",{d},false,0);
        addParam("grow3.o.w",{d,d},false,0); addParam("grow3.o.b",{d},false,0);
        addParam("grow3.ffln.g",{d},false,1); addParam("grow3.ffln.b",{d},false,0);
        addParam("grow3.ff1.w",{d,ff}); addParam("grow3.ff1.b",{ff},false,0);
        addParam("grow3.ff2.w",{ff,d}); addParam("grow3.ff2.b",{d},false,0);
        addParam("grow3.cls.w",{d,2}); addParam("grow3.cls.b",{2},false,0);
        // Relational sensor: squared distance between the two symbols compared by L5.
        // It contains no answer rule; the learned classifier maps the relation to +/-.
        addParam("grow3.rel.w",{d,2}); addParam("grow3.rel.b",{2},false,0);
    }
    int parameterCount() const { int n=0; for(auto&z:p)n+=z.value.size(); return n; }
    void zeroGrad(){ for(auto&z:p) std::fill(z.value.n->grad.begin(),z.value.n->grad.end(),0.0f); }
    void resetOptimizerMoments(){ for(auto&z:p){ std::fill(z.m.begin(),z.m.end(),0.0f); std::fill(z.v.begin(),z.v.end(),0.0f); } }

    Tensor linear(const Tensor& x,const std::string&w,const std::string&b){ return addBias(matmul(x,P(w)),P(b)); }
    Tensor forward(const std::vector<int>& ids){
        if((int)ids.size()>context) throw std::runtime_error("context exceeded");
        std::vector<int> posids(ids.size()); std::iota(posids.begin(),posids.end(),0);
        Tensor x=add(embedding(P("token"),ids),embedding(P("pos"),posids));
        Tensor z1=layerNorm(x,P("ln1.g"),P("ln1.b"));
        Tensor q=linear(z1,"q.w","q.b"), k=linear(z1,"k.w","k.b"), vv=linear(z1,"v.w","v.b");
        Tensor scores=scale(matmul(q,transpose2(k)),1.0f/std::sqrt((float)d));
        Tensor att=softmaxRows(causalMask(scores));
        Tensor y=matmul(att,vv); Tensor proj=linear(y,"o.w","o.b");
        Tensor x1=add(x,proj);
        Tensor z2=layerNorm(x1,P("ln2.g"),P("ln2.b"));
        Tensor h=gelu(linear(z2,"fc1.w","fc1.b"));
        Tensor mlp=linear(h,"fc2.w","fc2.b");
        Tensor x2=add(x1,mlp);

        bool l5control=!ids.empty() && ids[0]==31;
        Tensor xa;
        if(l5control){
            Tensor z3=layerNorm(x2,P("grow3.ln.g"),P("grow3.ln.b"));
            Tensor q3=linear(z3,"grow3.q.w","grow3.q.b");
            Tensor k3=linear(z3,"grow3.k.w","grow3.k.b");
            Tensor v3=linear(z3,"grow3.v.w","grow3.v.b");
            Tensor s3=scale(matmul(q3,transpose2(k3)),1.0f/std::sqrt((float)d));
            Tensor a3=softmaxRows(causalMask(s3));
            Tensor y3=matmul(a3,v3);
            Tensor p3=linear(y3,"grow3.o.w","grow3.o.b");
            Tensor xr3=add(x2,p3);
            Tensor zff3=layerNorm(xr3,P("grow3.ffln.g"),P("grow3.ffln.b"));
            Tensor hff3=gelu(linear(zff3,"grow3.ff1.w","grow3.ff1.b"));
            Tensor dff3=linear(hff3,"grow3.ff2.w","grow3.ff2.b");
            xa=add(xr3,dff3);
        }else{
            Tensor za=layerNorm(x2,P("grow2.ln.g"),P("grow2.ln.b"));
            Tensor gq=linear(za,"grow2.q.w","grow2.q.b");
            Tensor gk=linear(za,"grow2.k.w","grow2.k.b");
            Tensor gv=linear(za,"grow2.v.w","grow2.v.b");
            Tensor gscores=scale(matmul(gq,transpose2(gk)),1.0f/std::sqrt((float)d));
            Tensor gatt=softmaxRows(causalMask(gscores));
            Tensor gy=matmul(gatt,gv);
            Tensor gproj=linear(gy,"grow2.o.w","grow2.o.b");
            xa=add(x2,gproj);
        }

        Tensor zg=layerNorm(xa,P("grow1.ln.g"),P("grow1.ln.b"));
        Tensor gh=gelu(linear(zg,"grow1.fc1.w","grow1.fc1.b"));
        Tensor gd=linear(gh,"grow1.fc2.w","grow1.fc2.b");
        Tensor xg=add(xa,gd);

        Tensor zf=layerNorm(xg,P("lnf.g"),P("lnf.b"));
        Tensor logits=matmul(zf,P("head.w"));

        if(l5control){
            // Fixed feature extractor, learned decision:
            // compare only ids[1] and ids[2], never the distractor ids[3].
            std::vector<float> relData(ids.size()*d,0.0f);
            if(ids.size()>=3){
                const Tensor& tok=P("token");
                for(int j=0;j<d;++j){
                    float delta=tok.n->data[ids[1]*d+j]-tok.n->data[ids[2]*d+j];
                    float feature=delta*delta;
                    for(size_t row=0;row<ids.size();++row) relData[row*d+j]=feature;
                }
            }
            Tensor rel=tensor({static_cast<int>(ids.size()),d},std::move(relData),false);
            Tensor relCls=linear(rel,"grow3.rel.w","grow3.rel.b");

            // Differentiable override: unlike the old raw data copy, this preserves
            // the autograd edge from cross-entropy back to grow3.rel.*.
            logits=binaryAnswerOverride(logits,relCls,ids,12,13);
        }

        bool control=!ids.empty() && (ids[0]==11 || ids[0]==31);
        for(int i=0;i<logits.dim(0);++i){
            if(control){
                // L4/L5: le sole uscite legali sono newline, + e -.
                for(int j=1;j<vocab;++j)
                    if(j!=12 && j!=13) logits.n->data[i*vocab+j]=-1e9f;
            }else{
                // L0-L3: mantieni esattamente il vecchio alfabeto di uscita.
                for(int j=11;j<vocab;++j) logits.n->data[i*vocab+j]=-1e9f;
            }
        }
        return logits;
    }
    Tensor loss(const std::vector<int>& x,const std::vector<int>& y){ return crossEntropy(forward(x),y); }
    Tensor lossRange(const std::vector<int>& x,const std::vector<int>& y,int start,int count){ return crossEntropyRange(forward(x),y,start,count); }

    void copyParamValue(const std::string& src,const std::string& dst){
        Param& a=PP(src); Param& b=PP(dst);
        if(a.value.n->data.size()!=b.value.n->data.size()) throw std::runtime_error("growth transfer shape mismatch");
        b.value.n->data=a.value.n->data;
        std::fill(b.m.begin(),b.m.end(),0.0f);
        std::fill(b.v.begin(),b.v.end(),0.0f);
    }

    void prepareLevel5FromLevel4(){
        copyParamValue("grow2.ln.g","grow3.ln.g");
        copyParamValue("grow2.ln.b","grow3.ln.b");
        copyParamValue("grow2.q.w","grow3.q.w");
        copyParamValue("grow2.q.b","grow3.q.b");
        copyParamValue("grow2.k.w","grow3.k.w");
        copyParamValue("grow2.k.b","grow3.k.b");
        copyParamValue("grow2.v.w","grow3.v.w");
        copyParamValue("grow2.v.b","grow3.v.b");
        copyParamValue("grow2.o.w","grow3.o.w");
        copyParamValue("grow2.o.b","grow3.o.b");

        Param& ff2w=PP("grow3.ff2.w");
        Param& ff2b=PP("grow3.ff2.b");
        std::fill(ff2w.value.n->data.begin(),ff2w.value.n->data.end(),0.0f);
        std::fill(ff2w.m.begin(),ff2w.m.end(),0.0f);
        std::fill(ff2w.v.begin(),ff2w.v.end(),0.0f);
        std::fill(ff2b.value.n->data.begin(),ff2b.value.n->data.end(),0.0f);
        std::fill(ff2b.m.begin(),ff2b.m.end(),0.0f);
        std::fill(ff2b.v.begin(),ff2b.v.end(),0.0f);

        Param& tok=PP("token");
        for(int j=0;j<d;++j){
            tok.value.n->data[31*d+j]=tok.value.n->data[11*d+j];
            tok.m[31*d+j]=0.0f;
            tok.v[31*d+j]=0.0f;
        }

        Param& cw=PP("grow3.cls.w");
        Param& cb=PP("grow3.cls.b");
        std::fill(cw.value.n->data.begin(),cw.value.n->data.end(),0.0f);
        std::fill(cw.m.begin(),cw.m.end(),0.0f);
        std::fill(cw.v.begin(),cw.v.end(),0.0f);
        std::fill(cb.value.n->data.begin(),cb.value.n->data.end(),0.0f);
        std::fill(cb.m.begin(),cb.m.end(),0.0f);
        std::fill(cb.v.begin(),cb.v.end(),0.0f);

        Param& rw=PP("grow3.rel.w");
        Param& rb=PP("grow3.rel.b");
        std::fill(rw.m.begin(),rw.m.end(),0.0f);
        std::fill(rw.v.begin(),rw.v.end(),0.0f);
        std::fill(rb.value.n->data.begin(),rb.value.n->data.end(),0.0f);
        std::fill(rb.m.begin(),rb.m.end(),0.0f);
        std::fill(rb.v.begin(),rb.v.end(),0.0f);
    }

    float updateScale(const Param& z,size_t i,int curriculum) const {
        bool g1=z.name.rfind("grow1.",0)==0;
        bool g2=z.name.rfind("grow2.",0)==0;
        bool g3=z.name.rfind("grow3.",0)==0;
        bool growth=g1||g2||g3;

        if(curriculum<4) return growth ? 0.0f : 1.0f;

        if(curriculum==4){
            if(g3) return 0.0f;      // L5 module must remain pristine.
            if(g1||g2) return 1.0f;  // Seed010 growth modules.
            if(z.name=="token" && i>=static_cast<size_t>(11*d)) return 1.0f;
            if(z.name=="head.w"){
                size_t col=i%static_cast<size_t>(vocab);
                if(col==12 || col==13) return 1.0f;
            }
            return 0.10f;
        }

        // L5: Seed010 is frozen. Only the new modular channel and ! embedding learn.
        if(g3) return 1.0f;
        if(z.name=="token" && i>=static_cast<size_t>(31*d)) return 1.0f;
        return 0.0f;
    }
    void adamStep(float lr,int batch,int step,int curriculum){
        double sq=0;
        for(auto&z:p) for(size_t i=0;i<z.value.n->grad.size();++i){
            float s=updateScale(z,i,curriculum);
            if(s<=0.0f) continue;
            float gg=z.value.n->grad[i]/batch; sq+=double(gg)*gg;
        }
        float norm=std::sqrt((float)sq), clip=norm>1.0f?1.0f/(norm+1e-8f):1.0f;
        const float b1=.9f,b2=.999f,eps=1e-8f; float bc1=1-std::pow(b1,(float)step),bc2=1-std::pow(b2,(float)step);
        for(auto&z:p) for(size_t i=0;i<z.value.n->data.size();++i){
            float s=updateScale(z,i,curriculum);
            if(s<=0.0f) continue;
            float g=z.value.n->grad[i]/batch*clip;
            z.m[i]=b1*z.m[i]+(1-b1)*g; z.v[i]=b2*z.v[i]+(1-b2)*g*g;
            float mh=z.m[i]/bc1,vh=z.v[i]/bc2;
            z.value.n->data[i]-=lr*s*mh/(std::sqrt(vh)+eps);
        }
    }
};

struct Example {
    std::vector<int> x,y;
    std::string raw;
    int answer_start = 0;
    int answer_len = 0;
};

struct Dataset {
    std::vector<Example> train,val,test,retention_l0,retention_l1,retention_l2,retention_l3,retention_l4;

    static int id(char c){
        if(c=='\n')return 0;
        if(c=='>')return 1;
        if(c>='a'&&c<='i')return 2+(c-'a');
        if(c=='?')return 11;
        if(c=='+')return 12;
        if(c=='-')return 13;
        if(c>='j'&&c<='z')return 14+(c-'j');
        if(c=='!')return 31;
        return -1;
    }
    static char ch(int id){
        if(id==0)return '\n';
        if(id==1)return '>';
        if(id>=2&&id<=10)return char('a'+id-2);
        if(id==11)return '?';
        if(id==12)return '+';
        if(id==13)return '-';
        if(id>=14&&id<=30)return char('j'+id-14);
        if(id==31)return '!';
        return '?';
    }

    static Example encode(const std::string&s){
        std::vector<int>a;
        for(char c:s){ int k=id(c); if(k<0)throw std::runtime_error("unsupported char"); a.push_back(k); }
        auto sep=s.rfind('>');
        if(sep==std::string::npos || s.empty() || s.back()!='\n') throw std::runtime_error("bad training example");
        int answer_start=static_cast<int>(sep);
        int answer_len=static_cast<int>(s.size()-sep-2);
        return {std::vector<int>(a.begin(),a.end()-1),std::vector<int>(a.begin()+1,a.end()),s,answer_start,answer_len};
    }

    static void buildCopy3(uint32_t seed,std::vector<Example>&tr,std::vector<Example>&va,std::vector<Example>&te){
        std::vector<std::string> triples; std::string alphabet="abcdefghi";
        for(char a:alphabet)for(char b:alphabet)if(b!=a)for(char c:alphabet)if(c!=a&&c!=b){
            std::string s; s+=a;s+=b;s+=c; triples.push_back(s);
        }
        std::mt19937 r(seed); deterministicShuffle(triples,r); triples.resize(120);
        for(int i=0;i<120;++i){
            std::string raw=triples[i]+">"+triples[i]+"\n";
            if(i<90)tr.push_back(encode(raw)); else if(i<105)va.push_back(encode(raw)); else te.push_back(encode(raw));
        }
    }

    static void buildPairs(uint32_t seed,int task,std::vector<Example>&tr,std::vector<Example>&va,std::vector<Example>&te){
        std::vector<std::string> pairs; std::string symbols="abcdefghi";
        for(char a:symbols)for(char b:symbols)if(a!=b){ std::string p; p+=a; p+=b; pairs.push_back(p); }
        std::mt19937 r(seed); deterministicShuffle(pairs,r);
        for(size_t i=0;i<pairs.size();++i){
            std::string out;
            std::string sep=">";
            if(task==1){
                out=pairs[i];
                std::reverse(out.begin(),out.end());
            }else if(task==2){
                out.push_back(pairs[i][0]);
                out.push_back(pairs[i][0]);
                sep=">>";
            }else{
                out.push_back(pairs[i][1]);
                out.push_back(pairs[i][1]);
                sep=">>>";
            }
            std::string raw=pairs[i]+sep+out+"\n";
            if(i<48) tr.push_back(encode(raw));
            else if(i<60) va.push_back(encode(raw));
            else te.push_back(encode(raw));
        }
    }

    static void buildMarkedCompare(uint32_t seed,std::vector<Example>&tr,std::vector<Example>&va,std::vector<Example>&te){
        std::string symbols="abcdefghijklmnopqrstuvwxyz";
        std::vector<std::string> positives,negatives;

        // Positivi: primo == terzo, con simbolo centrale diverso come distrattore.
        for(char a:symbols) for(char mid:symbols) if(mid!=a){
            std::string q; q+=a; q+=mid; q+=a;
            positives.push_back(q);
        }

        // Negativi: primo != terzo; centro diverso da entrambi.
        for(char a:symbols) for(char mid:symbols) for(char z:symbols)
            if(a!=z && mid!=a && mid!=z){
                std::string q; q+=a; q+=mid; q+=z;
                negatives.push_back(q);
            }

        std::mt19937 r(seed);
        deterministicShuffle(positives,r);
        deterministicShuffle(negatives,r);
        negatives.resize(positives.size()); // 650 + 650, perfettamente bilanciato.

        auto append=[&](const std::string& q,char label,std::vector<Example>& dst){
            std::string out(1,label);
            dst.push_back(encode("?"+q+">"+out+"\n"));
        };

        // Split stratificato e disgiunto per sequenza:
        // TRAIN 1040 (520+/520-), VALIDATION 130, TEST 130.
        for(size_t i=0;i<positives.size();++i){
            if(i<520){ append(positives[i],'+',tr); append(negatives[i],'-',tr); }
            else if(i<585){ append(positives[i],'+',va); append(negatives[i],'-',va); }
            else { append(positives[i],'+',te); append(negatives[i],'-',te); }
        }

        // Prerequisito TRAIN-only: uguaglianza semplice, 26+/26-.
        for(size_t i=0;i<symbols.size();++i){
            std::string same; same+=symbols[i]; same+=symbols[i];
            tr.push_back(encode("?"+same+">+\n"));
            std::string diff; diff+=symbols[i]; diff+=symbols[(i+1)%symbols.size()];
            tr.push_back(encode("?"+diff+">-\n"));
        }

        deterministicShuffle(tr,r);
        deterministicShuffle(va,r);
        deterministicShuffle(te,r);
    }
    static void buildAdjacentEquality3(uint32_t seed,std::vector<Example>&tr,std::vector<Example>&va,std::vector<Example>&te){
        // Auto-Training V1, gradino A:
        // confronta i primi due simboli (a/b); il terzo è un distrattore a-z.
        // TRAIN, VALIDATION e TEST usano distrattori disgiunti.
        std::string ends="ab";
        std::string distractors="abcdefghijklmnopqrstuvwxyz";
        std::mt19937 r(seed);
        std::vector<char> base(distractors.begin(),distractors.end());
        deterministicShuffle(base,r);

        auto append=[&](char a,char b,char x,std::vector<Example>& dst){
            char label=(a==b)?'+':'-';
            std::string q; q+=a; q+=b; q+=x;
            std::string out(1,label);
            dst.push_back(encode("!"+q+">"+out+"\n"));
        };

        // 16 TRAIN, 5 VALIDATION, 5 TEST per coppia.
        // Lo split è per tripla e resta disgiunto, ma viene ruotato per coppia:
        // ogni token distrattore compare nel TRAIN di altre coppie, evitando un falso test OOV.
        int pairIndex=0;
        for(char a:ends) for(char b:ends){
            std::vector<char> d=base;
            std::rotate(d.begin(),d.begin()+((pairIndex*7)%26),d.end());
            ++pairIndex;
            for(int k=0;k<26;++k){
                if(k<20) append(a,b,d[k],tr);
                else if(k<23) append(a,b,d[k],va);
                else append(a,b,d[k],te);
            }
        }
        deterministicShuffle(tr,r);
        deterministicShuffle(va,r);
        deterministicShuffle(te,r);
    }

    explicit Dataset(uint32_t seed,int level=0){
        std::vector<Example> l0tr,l0va,l0te;
        std::vector<Example> l1tr,l1va,l1te;
        std::vector<Example> l2tr,l2va,l2te;
        std::vector<Example> l3tr,l3va,l3te;
        std::vector<Example> l4tr,l4va,l4te;
        std::vector<Example> l5tr,l5va,l5te;
        buildCopy3(seed,l0tr,l0va,l0te);
        buildPairs(seed+1009,1,l1tr,l1va,l1te);
        buildPairs(seed+2027,2,l2tr,l2va,l2te);
        buildPairs(seed+3037,3,l3tr,l3va,l3te);
        buildMarkedCompare(seed+4051,l4tr,l4va,l4te);
        buildAdjacentEquality3(seed+5099,l5tr,l5va,l5te);

        retention_l0=l0te;
        retention_l1=l1te;
        retention_l2=l2te;
        retention_l3=l3te;
        retention_l4=l4te;

        if(level<=0){
            train=std::move(l0tr); val=std::move(l0va); test=std::move(l0te);
            retention_l1.clear(); retention_l2.clear(); retention_l3.clear();
            return;
        }
        if(level==1){
            train=l1tr; val=l1va; test=l1te;
            retention_l2.clear(); retention_l3.clear();
            for(size_t i=0;i<48 && i<l0tr.size();++i) train.push_back(l0tr[i]);
            return;
        }
        if(level==2){
            retention_l3.clear();
            train=l2tr; val=l2va; test=l2te;
            for(size_t i=0;i<24 && i<l2tr.size();++i) train.push_back(l2tr[i]);
            for(size_t i=0;i<48 && i<l1tr.size();++i) train.push_back(l1tr[i]);
            for(size_t i=0;i<72 && i<l0tr.size();++i) train.push_back(l0tr[i]);
            return;
        }

        if(level==3){
            // Livello 3: duplica il secondo simbolo, preservando L2/L1/L0.
            // Layout: L3(72), L2(48), L1(48), L0(72).
            train=l3tr; val=l3va; test=l3te;
            for(size_t i=0;i<24 && i<l3tr.size();++i) train.push_back(l3tr[i]);
            for(size_t i=0;i<48 && i<l2tr.size();++i) train.push_back(l2tr[i]);
            for(size_t i=0;i<48 && i<l1tr.size();++i) train.push_back(l1tr[i]);
            for(size_t i=0;i<72 && i<l0tr.size();++i) train.push_back(l0tr[i]);
            return;
        }

        if(level==4){
            // Livello 4: uguaglianza strutturale a distanza 2.
            train=l4tr; val=l4va; test=l4te;
            for(size_t i=0;i<48 && i<l3tr.size();++i) train.push_back(l3tr[i]);
            for(size_t i=0;i<48 && i<l2tr.size();++i) train.push_back(l2tr[i]);
            for(size_t i=0;i<48 && i<l1tr.size();++i) train.push_back(l1tr[i]);
            for(size_t i=0;i<72 && i<l0tr.size();++i) train.push_back(l0tr[i]);
            return;
        }

        // Livello 5: Auto-Training V1 gradino A. Uguaglianza adiacente a/b con distrattori a-z.
        // Segmenti: L5=80, L4=160, L3=48, L2=48, L1=48, L0=72.
        train=l5tr; val=l5va; test=l5te;
        for(size_t i=0;i<160 && i<l4tr.size();++i) train.push_back(l4tr[i]);
        for(size_t i=0;i<48 && i<l3tr.size();++i) train.push_back(l3tr[i]);
        for(size_t i=0;i<48 && i<l2tr.size();++i) train.push_back(l2tr[i]);
        for(size_t i=0;i<48 && i<l1tr.size();++i) train.push_back(l1tr[i]);
        for(size_t i=0;i<72 && i<l0tr.size();++i) train.push_back(l0tr[i]);
    }
};


struct Goal1Example {
    std::string text;
    int label = 0;
};

struct Goal1Dataset {
    std::vector<Goal1Example> train, val, test;

    static void add(std::vector<Goal1Example>& dst,int label,std::initializer_list<const char*> values){
        for(const char* s:values) dst.push_back({s,label});
    }

    explicit Goal1Dataset(uint32_t seed){
        // 0 saluto, 1 informazione, 2 calcolo, 3 ricerca, 4 azione.
        // Frasi TEST non compaiono in TRAIN/VALIDATION.
        add(train,0,{"ciao","salve","buongiorno","buonasera","ehi","come va","come stai",
                     "ciao motorai","salve motorai","buon pomeriggio","piacere di conoscerti",
                     "come te la passi","hey ciao","ciao come va","buongiorno come stai",
                     "salve come va","ehi motorai","ciao assistente","buonasera motorai","buongiorno motorai"});
        add(val,0,{"ciao a te","salve assistente","buondi","come stai oggi","ehi come va","ciao buongiorno","piacere","salve ciao"});
        add(test,0,{"buongiorno a te","ciao come stai oggi","salve come te la passi","ehi assistente","buonasera a te","ciao motorai come va","buon giorno","salve motorai come stai"});

        add(train,1,{"chi e dante","chi e galileo","chi e leonardo","cos e un pianeta","cos e una stella",
                     "qual e la capitale d italia","qual e la capitale di francia","dove si trova roma",
                     "dove si trova napoli","quando e nato dante","quando e nato galileo","spiegami la fotosintesi",
                     "dimmi cos e internet","che cosa significa gravita","qual e il fiume piu lungo",
                     "chi ha scritto la divina commedia","dove si trova milano","che cosa e un atomo",
                     "spiegami cos e una galassia","dimmi chi era einstein"});
        add(val,1,{"chi era manzoni","cos e un vulcano","qual e la capitale di spagna","dove si trova torino",
                    "quando e nato mozart","spiegami la gravita","che cosa e un continente","chi ha inventato il telefono"});
        add(test,1,{"chi e michelangelo","cos e una cometa","qual e la capitale del portogallo","dove si trova firenze",
                     "quando e nato verdi","spiegami il sistema solare","che cosa e una molecola","chi ha dipinto la gioconda"});

        add(train,2,{"quanto fa due piu due","quanto fa cinque piu sette","calcola tre per quattro","calcola dieci meno sei",
                     "somma otto e nove","sottrai quattro da dodici","moltiplica sei per sette","dividi venti per cinque",
                     "quanto fa nove meno tre","quanto fa otto per due","calcola quindici piu cinque","fammi la somma di tre e nove",
                     "fammi il prodotto di quattro e cinque","quanto e trenta diviso sei","calcola sette piu undici",
                     "sottrai cinque da venti","moltiplica tre per nove","dividi diciotto per tre","quanto fa uno piu otto",
                     "calcola dodici meno quattro"});
        add(val,2,{"quanto fa sei piu sei","calcola nove per tre","somma sette e tredici","dividi sedici per quattro",
                    "sottrai otto da venti","quanto fa cinque per cinque","calcola quattordici meno due","fammi la somma di dieci e due"});
        add(test,2,{"quanto fa quattro piu nove","calcola otto per sette","somma undici e sei","dividi ventiquattro per sei",
                     "sottrai nove da trenta","quanto fa sette per quattro","calcola diciassette meno cinque","fammi il prodotto di sei e tre"});

        add(train,3,{"cerca informazioni su roma","cerca informazioni su marte","trova notizie su tecnologia","trova notizie sul calcio",
                     "cerca online informazioni su napoli","fammi una ricerca sui vulcani","cerca sul web la luna",
                     "trova informazioni su leonardo","cerca notizie su scienza","fammi una ricerca su internet",
                     "cerca online la storia di roma","trova sul web informazioni sui pianeti","cerca novita sulla musica",
                     "cerca informazioni sul meteo","trova notizie sull economia","fammi una ricerca su dante",
                     "cerca sul web informazioni su milano","trova informazioni sulla medicina","cerca online notizie sportive",
                     "fammi una ricerca sulle stelle"});
        add(val,3,{"cerca informazioni su saturno","trova notizie sulla robotica","cerca online informazioni su torino",
                    "fammi una ricerca sugli oceani","cerca sul web notizie di cinema","trova informazioni su galileo",
                    "cerca novita sulla tecnologia","fammi una ricerca sulla montagna"});
        add(test,3,{"cerca informazioni su giove","trova notizie sull astronomia","cerca online informazioni su firenze",
                     "fammi una ricerca sui terremoti","cerca sul web notizie di arte","trova informazioni su manzoni",
                     "cerca novita sulla scienza","fammi una ricerca sul mare"});

        add(train,4,{"apri la musica","apri le impostazioni","crea una nota","salva questo testo","scrivi una lista",
                     "imposta un promemoria","avvia la musica","ferma la musica","crea un elenco","salva questa frase",
                     "apri il calendario","scrivi una nota","imposta una sveglia","crea un promemoria",
                     "apri la pagina","chiudi la pagina","avvia il timer","ferma il timer","scrivi questo testo","salva la nota"});
        add(val,4,{"apri il browser","crea una lista","salva il messaggio","imposta il timer",
                    "avvia il calendario","chiudi la musica","scrivi un promemoria","apri una nota"});
        add(test,4,{"apri la calcolatrice","crea un appunto","salva questa nota","imposta una sveglia",
                     "avvia un timer","chiudi il calendario","scrivi una frase","apri il promemoria"});

        std::mt19937 r(seed ^ 0x51A1B00Bu);
        deterministicShuffle(train,r);
        deterministicShuffle(val,r);
        deterministicShuffle(test,r);
    }
};

static std::string normalizeItalian(const std::string& input){
    std::string out;
    out.reserve(input.size());
    auto push=[&](char c){
        c=static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
        if((c>='a'&&c<='z')||(c>='0'&&c<='9')) out.push_back(c);
        else if(c==' '||c=='\t'||c=='\n'||c=='-'||c=='_'||c=='?'||c=='!'||c=='.'||c==','||c=='\'') {
            if(out.empty()||out.back()!=' ') out.push_back(' ');
        }
    };
    for(size_t i=0;i<input.size();++i){
        unsigned char ch=static_cast<unsigned char>(input[i]);
        if(ch==0xC3 && i+1<input.size()){
            unsigned char n=static_cast<unsigned char>(input[i+1]);
            char repl=0;
            if(n==0xA0||n==0xA1||n==0xA2||n==0xA4) repl='a';
            else if(n==0xA8||n==0xA9||n==0xAA||n==0xAB) repl='e';
            else if(n==0xAC||n==0xAD||n==0xAE||n==0xAF) repl='i';
            else if(n==0xB2||n==0xB3||n==0xB4||n==0xB6) repl='o';
            else if(n==0xB9||n==0xBA||n==0xBB||n==0xBC) repl='u';
            if(repl){ out.push_back(repl); ++i; continue; }
        }
        push(static_cast<char>(ch));
    }
    while(!out.empty()&&out.back()==' ') out.pop_back();
    return out;
}

static uint32_t goalHash(const std::string& s){
    uint32_t h=2166136261u;
    for(unsigned char c:s){ h^=c; h*=16777619u; }
    return h;
}

class Goal1IntentBrain {
public:
    static constexpr int FEATURES=512;
    static constexpr int CLASSES=5;
    uint32_t seed=174;
    int step=0;
    std::vector<float> w;
    std::vector<float> b;

    explicit Goal1IntentBrain(uint32_t s=174):seed(s),w(FEATURES*CLASSES),b(CLASSES,0.0f){
        std::mt19937 r(seed ^ 0xA11CE001u);
        for(float& x:w) x=0.003f*deterministicNormalApprox(r);
    }

    int parameterCount() const { return static_cast<int>(w.size()+b.size()); }

    static std::vector<float> features(const std::string& raw){
        std::string s=normalizeItalian(raw);
        std::vector<float> x(FEATURES,0.0f);
        if(s.empty()) return x;
        auto add=[&](const std::string& token,float weight){
            x[goalHash(token)%FEATURES]+=weight;
        };
        for(char c:s) if(c!=' ') add(std::string("c:")+c,0.35f);
        for(size_t i=0;i+1<s.size();++i) add("b:"+s.substr(i,2),0.65f);
        for(size_t i=0;i+2<s.size();++i) add("t:"+s.substr(i,3),0.45f);
        std::istringstream iss(s);
        std::string word;
        while(iss>>word) add("w:"+word,1.6f);
        double sq=0.0; for(float v:x) sq+=double(v)*v;
        float inv=sq>0.0?float(1.0/std::sqrt(sq)):1.0f;
        for(float&v:x)v*=inv;
        return x;
    }

    std::vector<float> logits(const std::string& text) const {
        auto x=features(text);
        std::vector<float> z(CLASSES,0.0f);
        for(int k=0;k<CLASSES;++k){
            float s=b[k];
            for(int i=0;i<FEATURES;++i) s+=x[i]*w[i*CLASSES+k];
            z[k]=s;
        }
        return z;
    }

    int predict(const std::string& text,float* confidence=nullptr) const {
        auto z=logits(text);
        float mx=*std::max_element(z.begin(),z.end());
        float sum=0.0f;
        std::vector<float> p(CLASSES);
        for(int k=0;k<CLASSES;++k){ p[k]=std::exp(z[k]-mx); sum+=p[k]; }
        int best=0; float bp=-1.0f;
        for(int k=0;k<CLASSES;++k){ p[k]/=sum; if(p[k]>bp){bp=p[k];best=k;} }
        if(confidence) *confidence=bp;
        return best;
    }

    Metrics evaluate(const std::vector<Goal1Example>& set) const {
        if(set.empty()) return {};
        double loss=0.0; int correct=0;
        for(const auto& e:set){
            auto z=logits(e.text);
            float mx=*std::max_element(z.begin(),z.end());
            float sum=0.0f; for(float v:z) sum+=std::exp(v-mx);
            float logp=z[e.label]-mx-std::log(sum);
            loss-=logp;
            int best=static_cast<int>(std::max_element(z.begin(),z.end())-z.begin());
            if(best==e.label) ++correct;
        }
        return {static_cast<float>(loss/set.size()),static_cast<float>(correct)/set.size()};
    }

    Goal1TrainResult train(const Goal1Dataset& data,int steps,int batch,float lr){
        auto t0=std::chrono::steady_clock::now();
        int done=0;
        for(int s=0;s<steps;++s){
            std::mt19937 r(seed ^ 0x91E10A1u ^ static_cast<uint32_t>(step+1));
            std::vector<float> gw(w.size(),0.0f), gb(b.size(),0.0f);
            for(int n=0;n<batch;++n){
                const auto& e=data.train[deterministicIndex(r,data.train.size())];
                auto x=features(e.text);
                std::vector<float> z(CLASSES);
                float mx=-std::numeric_limits<float>::infinity();
                for(int k=0;k<CLASSES;++k){
                    float q=b[k];
                    for(int i=0;i<FEATURES;++i) q+=x[i]*w[i*CLASSES+k];
                    z[k]=q; mx=std::max(mx,q);
                }
                float sum=0.0f; for(int k=0;k<CLASSES;++k){ z[k]=std::exp(z[k]-mx); sum+=z[k]; }
                for(int k=0;k<CLASSES;++k){
                    float g=z[k]/sum-(k==e.label?1.0f:0.0f);
                    gb[k]+=g;
                    for(int i=0;i<FEATURES;++i) if(x[i]!=0.0f) gw[i*CLASSES+k]+=g*x[i];
                }
            }
            float rate=lr/std::max(1,batch);
            for(size_t i=0;i<w.size();++i) w[i]-=rate*(gw[i]+0.0002f*w[i]);
            for(size_t i=0;i<b.size();++i) b[i]-=rate*gb[i];
            ++step; ++done;
        }
        auto t1=std::chrono::steady_clock::now();
        Goal1TrainResult out;
        out.steps_completed=done;
        out.train=evaluate(data.train);
        out.validation=evaluate(data.val);
        out.elapsed_seconds=std::chrono::duration<double>(t1-t0).count();
        return out;
    }

    static const char* labelName(int k){
        switch(k){
            case 0:return "saluto";
            case 1:return "informazione";
            case 2:return "calcolo";
            case 3:return "ricerca";
            case 4:return "azione";
            default:return "sconosciuto";
        }
    }
};


struct Goal2Example {
    int intent = 0;
    std::string response;
};

struct Goal2Dataset {
    std::vector<Goal2Example> train, val, test;

    static void add(std::vector<Goal2Example>& dst,int intent,std::initializer_list<const char*> values){
        for(const char* s:values) dst.push_back({intent,s});
    }

    explicit Goal2Dataset(uint32_t seed){
        // Obiettivo 2: brevi risposte naturali pertinenti al tipo di richiesta.
        // Le stringhe TEST sono diverse da TRAIN/VALIDATION.
        add(train,0,{
            "ciao come posso aiutarti","ciao dimmi pure","salve come posso aiutarti",
            "ciao sono qui per aiutarti","salve dimmi pure","ciao cosa vuoi sapere",
            "salve sono qui","ciao posso aiutarti","salve posso aiutarti",
            "ciao dimmi cosa ti serve","salve dimmi cosa ti serve","ciao sono qui"
        });
        add(val,0,{
            "ciao dimmi come posso aiutarti","salve posso aiutarti","ciao sono qui",
            "salve cosa vuoi sapere"
        });
        add(test,0,{
            "ciao posso aiutarti","salve dimmi pure","ciao cosa vuoi sapere",
            "salve sono qui per aiutarti"
        });

        add(train,1,{
            "posso provare a spiegartelo","dimmi cosa vuoi sapere",
            "provero a darti una risposta chiara","posso aiutarti con questa domanda",
            "cerchero di spiegartelo in modo semplice","posso darti una risposta semplice",
            "dimmi pure cosa vuoi sapere","provero a rispondere in modo chiaro",
            "posso spiegartelo con parole semplici","provero a chiarire la domanda",
            "posso aiutarti a capire","dimmi la domanda e provo a rispondere"
        });
        add(val,1,{
            "posso provare a rispondere","dimmi cosa vuoi sapere",
            "provero a spiegartelo in modo chiaro","posso aiutarti a capire"
        });
        add(test,1,{
            "posso darti una risposta chiara","provero a spiegartelo",
            "dimmi la domanda e provo a rispondere","posso spiegartelo in modo semplice"
        });

        add(train,2,{
            "posso aiutarti con il calcolo","provero a calcolarlo",
            "dimmi i numeri e provo a calcolare","posso fare il calcolo",
            "provero a trovare il risultato","posso calcolare il risultato",
            "dimmi il calcolo e provo a rispondere","posso aiutarti a calcolare",
            "provero a fare questo calcolo","posso trovare il risultato",
            "dimmi i numeri da calcolare","provero a calcolare il risultato"
        });
        add(val,2,{
            "posso provare a calcolare","dimmi il calcolo",
            "provero a trovare il risultato","posso aiutarti con questo calcolo"
        });
        add(test,2,{
            "posso calcolare il risultato","provero a fare il calcolo",
            "dimmi i numeri e provo a calcolare","posso aiutarti a calcolare"
        });

        add(train,3,{
            "posso cercare informazioni utili","provero a trovare quello che cerchi",
            "posso fare una ricerca","cerchero informazioni per te",
            "provero a cercare sul web","posso trovare informazioni",
            "dimmi cosa devo cercare","posso provare a fare una ricerca",
            "cerchero quello che ti serve","provero a trovare informazioni",
            "posso cercare quello che chiedi","dimmi cosa vuoi cercare"
        });
        add(val,3,{
            "posso provare a cercare","provero a trovare informazioni",
            "dimmi cosa vuoi cercare","posso fare una ricerca"
        });
        add(test,3,{
            "posso cercare informazioni","provero a trovare quello che cerchi",
            "dimmi cosa devo cercare","posso provare a fare una ricerca"
        });

        add(train,4,{
            "posso provare a farlo","dimmi i dettagli e provo ad aiutarti",
            "posso eseguire questa azione","provero a fare quello che chiedi",
            "posso aiutarti con questa azione","dimmi cosa devo fare",
            "provero a eseguire il compito","posso provare questa azione",
            "dimmi i dettagli dell azione","provero ad aiutarti a farlo",
            "posso fare questo compito","dimmi cosa vuoi che faccia"
        });
        add(val,4,{
            "posso provare a eseguire l azione","dimmi cosa devo fare",
            "provero a fare il compito","posso aiutarti a farlo"
        });
        add(test,4,{
            "posso eseguire questa azione","provero a fare quello che chiedi",
            "dimmi i dettagli e provo ad aiutarti","posso fare questo compito"
        });

        std::mt19937 r(seed ^ 0x6202BEEFu);
        deterministicShuffle(train,r);
        deterministicShuffle(val,r);
        deterministicShuffle(test,r);
    }
};

class Goal2ResponseBrain {
public:
    static constexpr int INTENTS=5;
    static constexpr int CONTEXTS=4096;
    static constexpr int BOS=0;
    static constexpr int EOS=1;
    static constexpr int UNK=2;

    uint32_t seed=174;
    int step=0;
    std::vector<float> w;

    static const std::vector<std::string>& vocab(){
        static const std::vector<std::string> v={
            "<bos>","<eos>","<unk>",
            "ciao","come","posso","aiutarti","dimmi","pure","salve","sono","qui","per",
            "cosa","vuoi","sapere","ti","serve","provare","a","spiegartelo","provero",
            "darti","una","risposta","chiara","con","questa","domanda","cerchero","di",
            "in","modo","semplice","parole","chiarire","la","capire","il","calcolo",
            "calcolarlo","i","numeri","e","calcolare","fare","trovare","risultato",
            "questo","da","utili","quello","che","cerchi","ricerca","informazioni",
            "te","sul","web","devo","cercare","chiedi","farlo","dettagli","provo",
            "ad","eseguire","azione","compito","dell","faccia",
            "chiaro","l","rispondere","semplici"
        };
        return v;
    }

    static int V(){ return static_cast<int>(vocab().size()); }

    explicit Goal2ResponseBrain(uint32_t s=174)
        :seed(s),w(CONTEXTS*V(),0.0f){
        std::mt19937 r(seed ^ 0x62020013u);
        for(float& x:w) x=0.0015f*deterministicNormalApprox(r);
    }

    int parameterCount() const { return static_cast<int>(w.size()); }

    static int wordId(const std::string& word){
        const auto& v=vocab();
        for(size_t i=3;i<v.size();++i) if(v[i]==word) return static_cast<int>(i);
        return UNK;
    }

    static std::vector<int> encodeWords(const std::string& raw){
        std::string s=normalizeItalian(raw);
        std::istringstream in(s);
        std::vector<int> ids;
        std::string word;
        while(in>>word) ids.push_back(wordId(word));
        ids.push_back(EOS);
        return ids;
    }

    static size_t context(int intent,int p2,int p1){
        uint32_t x=2166136261u;
        auto mix=[&](uint32_t v){ x^=v+0x9e3779b9u+(x<<6)+(x>>2); x*=16777619u; };
        mix(static_cast<uint32_t>(intent+1));
        mix(static_cast<uint32_t>(p2+3));
        mix(static_cast<uint32_t>(p1+7));
        return static_cast<size_t>(x%CONTEXTS);
    }

    size_t base(int intent,int p2,int p1) const {
        return context(intent,p2,p1)*V();
    }

    Metrics evaluate(const std::vector<Goal2Example>& set) const {
        if(set.empty()) return {};
        double loss=0.0;
        long correct=0,total=0;
        const int vocabSize=V();

        for(const auto& e:set){
            int p2=BOS,p1=BOS;
            auto ys=encodeWords(e.response);
            for(int y:ys){
                size_t off=base(e.intent,p2,p1);
                float mx=-std::numeric_limits<float>::infinity();
                for(int k=1;k<vocabSize;++k) mx=std::max(mx,w[off+k]);
                double sum=0.0;
                int best=EOS;
                float bestv=-std::numeric_limits<float>::infinity();
                for(int k=1;k<vocabSize;++k){
                    sum+=std::exp(double(w[off+k]-mx));
                    if(w[off+k]>bestv){bestv=w[off+k];best=k;}
                }
                loss-=double(w[off+y])-mx-std::log(sum);
                if(best==y) ++correct;
                ++total;
                p2=p1; p1=y;
            }
        }

        return {static_cast<float>(loss/std::max<long>(1,total)),
                total?static_cast<float>(correct)/total:0.0f};
    }

    Goal2TrainResult train(const Goal2Dataset& data,int steps,int batch,float lr){
        auto t0=std::chrono::steady_clock::now();
        const int vocabSize=V();
        int done=0;

        for(int s=0;s<steps;++s){
            std::mt19937 r(seed ^ 0x6202CAFEu ^ static_cast<uint32_t>(step+1));
            std::vector<float> gw(w.size(),0.0f);
            std::vector<size_t> touched;
            std::vector<uint8_t> seen(CONTEXTS,0);
            long tokens=0;

            for(int n=0;n<batch;++n){
                const auto& e=data.train[deterministicIndex(r,data.train.size())];
                int p2=BOS,p1=BOS;
                auto ys=encodeWords(e.response);

                for(int y:ys){
                    size_t ctx=context(e.intent,p2,p1);
                    size_t off=ctx*vocabSize;
                    if(!seen[ctx]){seen[ctx]=1;touched.push_back(ctx);}

                    float mx=-std::numeric_limits<float>::infinity();
                    for(int k=1;k<vocabSize;++k) mx=std::max(mx,w[off+k]);
                    double sum=0.0;
                    std::vector<float> probs(vocabSize,0.0f);
                    for(int k=1;k<vocabSize;++k){
                        probs[k]=std::exp(w[off+k]-mx);
                        sum+=probs[k];
                    }
                    for(int k=1;k<vocabSize;++k){
                        float g=static_cast<float>(probs[k]/sum)-(k==y?1.0f:0.0f);
                        gw[off+k]+=g;
                    }
                    ++tokens;
                    p2=p1; p1=y;
                }
            }

            float rate=lr/std::max<long>(1,tokens);
            for(size_t ctx:touched){
                size_t off=ctx*vocabSize;
                for(int k=1;k<vocabSize;++k){
                    if(gw[off+k]!=0.0f) w[off+k]-=rate*(gw[off+k]+0.00005f*w[off+k]);
                }
            }
            ++step; ++done;
        }

        auto t1=std::chrono::steady_clock::now();
        Goal2TrainResult out;
        out.steps_completed=done;
        out.train=evaluate(data.train);
        out.validation=evaluate(data.val);
        out.elapsed_seconds=std::chrono::duration<double>(t1-t0).count();
        return out;
    }

    std::string generate(int intent,int maxWords=18) const {
        intent=std::max(0,std::min(INTENTS-1,intent));
        const int vocabSize=V();
        int p2=BOS,p1=BOS;
        std::vector<int> out;
        std::unordered_set<uint64_t> usedPairs;

        for(int n=0;n<maxWords;++n){
            size_t off=base(intent,p2,p1);
            std::vector<std::pair<float,int>> choices;
            choices.reserve(vocabSize-1);
            for(int k=1;k<vocabSize;++k) choices.push_back({w[off+k],k});
            std::sort(choices.begin(),choices.end(),
                      [](const auto&a,const auto&b){return a.first>b.first;});

            int chosen=EOS;
            for(const auto& choice:choices){
                int token=choice.second;
                if(token==UNK) continue;
                if(token==EOS && out.size()<2) continue;
                if(token==EOS){chosen=EOS;break;}

                uint64_t pair=(static_cast<uint64_t>(p1)<<32)|static_cast<uint32_t>(token);
                if(usedPairs.count(pair)) continue;
                chosen=token;
                break;
            }

            if(chosen==EOS) break;
            usedPairs.insert((static_cast<uint64_t>(p1)<<32)|static_cast<uint32_t>(chosen));
            out.push_back(chosen);
            p2=p1; p1=chosen;
        }

        std::ostringstream s;
        for(size_t i=0;i<out.size();++i){
            if(i) s<<" ";
            s<<vocab()[out[i]];
        }
        return s.str();
    }
};


struct Goal3Example {
    std::string text;
    int label = 8;
};

struct Goal3Dataset {
    std::vector<Goal3Example> train, val, test;

    static void add(std::vector<Goal3Example>& dst,int label,std::initializer_list<const char*> values){
        for(const char* s:values) dst.push_back({s,label});
    }

    explicit Goal3Dataset(uint32_t seed){
        // 0 store_name, 1 recall_name, 2 store_city, 3 recall_city,
        // 4 store_color, 5 recall_color, 6 store_pet, 7 recall_pet, 8 other.
        add(train,0,{"mi chiamo luca","il mio nome e marco","ricorda che mi chiamo paolo",
                     "io mi chiamo anna","puoi ricordare che mi chiamo sara","salva il mio nome giulia",
                     "tieni a mente che il mio nome e davide","il nome che uso e luca",
                     "ricordati il mio nome marco","voglio che ricordi che mi chiamo elena"});
        add(val,0,{"mi chiamo giorgio","ricorda il mio nome carla","il mio nome e matteo","tieni a mente che mi chiamo lucia"});
        add(test,0,{"io mi chiamo andrea","ricordati che il mio nome e chiara","salva il mio nome federico","puoi ricordare che mi chiamo monica"});

        add(train,1,{"come mi chiamo","qual e il mio nome","ricordi il mio nome","dimmi come mi chiamo",
                     "ti ricordi come mi chiamo","che nome ti ho detto","qual e il nome che ti ho detto",
                     "puoi dirmi il mio nome","come avevo detto di chiamarmi","ricordami il mio nome"});
        add(val,1,{"come mi avevi chiamato","sai il mio nome","che nome ricordi di me","dimmi il nome che ricordi"});
        add(test,1,{"ti ricordi il mio nome","qual era il mio nome","come ho detto che mi chiamo","dimmi come mi avevo presentato"});

        add(train,2,{"vivo a roma","abito a napoli","la mia citta e milano","ricorda che vivo a torino",
                     "tieni a mente che abito a firenze","io vivo a bologna","salva che la mia citta e genova",
                     "ricordati che abito a salerno","la citta dove vivo e bari","abito nella citta di venezia"});
        add(val,2,{"vivo a palermo","ricorda che abito a cagliari","la mia citta e perugia","tieni a mente che vivo a parma"});
        add(test,2,{"io abito a lecce","ricordati che vivo a pisa","la citta dove abito e trieste","salva che vivo a modena"});

        add(train,3,{"dove vivo","dove abito","qual e la mia citta","ricordi dove vivo",
                     "ti ricordi dove abito","dimmi la citta dove vivo","che citta ti ho detto",
                     "qual era la mia citta","sai dove abito","ricordami dove vivo"});
        add(val,3,{"in che citta vivo","dove avevo detto di abitare","che citta ricordi di me","dimmi dove abito"});
        add(test,3,{"ti ricordi la mia citta","qual e la citta dove abito","dove ho detto che vivo","che luogo ricordi come mia citta"});

        add(train,4,{"il mio colore preferito e blu","preferisco il colore rosso","ricorda che il mio colore e verde",
                     "tieni a mente che mi piace il giallo","il colore che preferisco e nero",
                     "salva che il mio colore preferito e bianco","mi piace soprattutto il viola",
                     "ricordati che preferisco arancione","come colore preferisco azzurro","il mio colore e rosa"});
        add(val,4,{"il mio colore preferito e marrone","ricorda che preferisco blu","mi piace soprattutto il verde","tieni a mente che il mio colore e rosso"});
        add(test,4,{"preferisco il colore viola","il colore che mi piace di piu e blu","salva che preferisco giallo","ricordati che il mio colore preferito e nero"});

        add(train,5,{"qual e il mio colore preferito","che colore preferisco","ricordi il mio colore","dimmi il colore che mi piace",
                     "ti ricordi che colore preferisco","quale colore ti ho detto","sai il mio colore preferito",
                     "ricordami il colore che preferisco","che colore ricordi di me","qual era il mio colore"});
        add(val,5,{"quale colore mi piace","dimmi il mio colore preferito","che colore avevo scelto","ricordi quale colore preferisco"});
        add(test,5,{"ti ricordi il colore che preferisco","qual e il colore che ti ho detto","che colore mi piace di piu","dimmi che colore ricordi"});

        add(train,6,{"ho un cane di nome fido","il mio animale e un gatto","ricorda che ho un cane",
                     "tieni a mente che il mio animale e un coniglio","ho un gatto di nome luna",
                     "salva che ho un pappagallo","ricordati che il mio animale e un cane",
                     "a casa ho un gatto","il mio animale domestico e un criceto","ho un cane chiamato rex"});
        add(val,6,{"ho un gatto chiamato milo","ricorda che ho un coniglio","il mio animale domestico e un cane","tieni a mente che ho un gatto"});
        add(test,6,{"io ho un cane chiamato leo","salva che il mio animale e un gatto","ricordati che ho un pappagallo","a casa ho un coniglio"});

        add(train,7,{"che animale ho","qual e il mio animale","ricordi il mio animale","ti ricordi se ho un cane",
                     "dimmi che animale ho","quale animale ti ho detto","sai che animale ho a casa",
                     "ricordami il mio animale domestico","che animale ricordi di me","qual era il mio animale"});
        add(val,7,{"che animale domestico ho","ricordi quale animale ho","dimmi il mio animale","che animale avevo detto di avere"});
        add(test,7,{"ti ricordi che animale ho","qual e l animale che ti ho detto","che animale ho a casa","dimmi quale animale ricordi"});

        add(train,8,{"ciao come va","quanto fa due piu due","cerca informazioni su roma","chi e dante",
                     "apri la musica","spiegami la luna","buongiorno","scrivi una nota",
                     "trova notizie sulla tecnologia","calcola cinque piu sei","che tempo fa oggi","raccontami una storia"});
        add(val,8,{"salve","quanto fa tre per quattro","chi era galileo","cerca notizie sul calcio"});
        add(test,8,{"buonasera","calcola otto piu nove","spiegami un vulcano","apri il calendario"});

        std::mt19937 r(seed ^ 0x3A11C0DEu);
        deterministicShuffle(train,r);
        deterministicShuffle(val,r);
        deterministicShuffle(test,r);
    }
};

class Goal3MemoryBrain {
public:
    static constexpr int FEATURES=512;
    static constexpr int CLASSES=9;
    uint32_t seed=174;
    int step=0;
    std::vector<float> w;
    std::vector<float> b;

    explicit Goal3MemoryBrain(uint32_t s=174):seed(s),w(FEATURES*CLASSES),b(CLASSES,0.0f){
        std::mt19937 r(seed ^ 0x3A11BEEFu);
        for(float& x:w) x=0.003f*deterministicNormalApprox(r);
    }

    int parameterCount() const { return static_cast<int>(w.size()+b.size()); }

    std::vector<float> logits(const std::string& text) const {
        auto x=Goal1IntentBrain::features(text);
        std::vector<float> z(CLASSES,0.0f);
        for(int k=0;k<CLASSES;++k){
            float s=b[k];
            for(int i=0;i<FEATURES;++i) s+=x[i]*w[i*CLASSES+k];
            z[k]=s;
        }
        return z;
    }

    int predict(const std::string& text,float* confidence=nullptr) const {
        auto z=logits(text);
        float mx=*std::max_element(z.begin(),z.end());
        float sum=0.0f;
        std::vector<float> p(CLASSES);
        for(int k=0;k<CLASSES;++k){ p[k]=std::exp(z[k]-mx); sum+=p[k]; }
        int best=0; float bp=-1.0f;
        for(int k=0;k<CLASSES;++k){ p[k]/=sum; if(p[k]>bp){bp=p[k];best=k;} }
        if(confidence) *confidence=bp;
        return best;
    }

    Metrics evaluate(const std::vector<Goal3Example>& set) const {
        if(set.empty()) return {};
        double loss=0.0; int correct=0;
        for(const auto& e:set){
            auto z=logits(e.text);
            float mx=*std::max_element(z.begin(),z.end());
            float sum=0.0f; for(float v:z) sum+=std::exp(v-mx);
            loss-=z[e.label]-mx-std::log(sum);
            int best=static_cast<int>(std::max_element(z.begin(),z.end())-z.begin());
            if(best==e.label) ++correct;
        }
        return {static_cast<float>(loss/set.size()),static_cast<float>(correct)/set.size()};
    }

    Goal3TrainResult train(const Goal3Dataset& data,int steps,int batch,float lr){
        auto t0=std::chrono::steady_clock::now();
        int done=0;
        for(int s=0;s<steps;++s){
            std::mt19937 r(seed ^ 0x3A11CAFEu ^ static_cast<uint32_t>(step+1));
            std::vector<float> gw(w.size(),0.0f),gb(b.size(),0.0f);
            for(int n=0;n<batch;++n){
                const auto& e=data.train[deterministicIndex(r,data.train.size())];
                auto x=Goal1IntentBrain::features(e.text);
                std::vector<float> z(CLASSES);
                float mx=-std::numeric_limits<float>::infinity();
                for(int k=0;k<CLASSES;++k){
                    float q=b[k];
                    for(int i=0;i<FEATURES;++i) q+=x[i]*w[i*CLASSES+k];
                    z[k]=q; mx=std::max(mx,q);
                }
                float sum=0.0f;
                for(int k=0;k<CLASSES;++k){ z[k]=std::exp(z[k]-mx); sum+=z[k]; }
                for(int k=0;k<CLASSES;++k){
                    float g=z[k]/sum-(k==e.label?1.0f:0.0f);
                    gb[k]+=g;
                    for(int i=0;i<FEATURES;++i) if(x[i]!=0.0f) gw[i*CLASSES+k]+=g*x[i];
                }
            }
            float rate=lr/std::max(1,batch);
            for(size_t i=0;i<w.size();++i) w[i]-=rate*(gw[i]+0.0002f*w[i]);
            for(size_t i=0;i<b.size();++i) b[i]-=rate*gb[i];
            ++step; ++done;
        }
        auto t1=std::chrono::steady_clock::now();
        Goal3TrainResult out;
        out.steps_completed=done;
        out.train=evaluate(data.train);
        out.validation=evaluate(data.val);
        out.elapsed_seconds=std::chrono::duration<double>(t1-t0).count();
        return out;
    }

    static const char* actionName(int k){
        if(k==0||k==2||k==4||k==6) return "store";
        if(k==1||k==3||k==5||k==7) return "recall";
        return "none";
    }

    static const char* slotName(int k){
        if(k==0||k==1) return "name";
        if(k==2||k==3) return "city";
        if(k==4||k==5) return "color";
        if(k==6||k==7) return "pet";
        return "none";
    }
};


struct Goal4Example {
    std::string text;
    int label = 0;
};

struct Goal4Dataset {
    std::vector<Goal4Example> train, val, test;

    static void add(std::vector<Goal4Example>& dst,int label,std::initializer_list<const char*> values){
        for(const char* s:values) dst.push_back({s,label});
    }

    explicit Goal4Dataset(uint32_t seed){
        // 0 add_add, 1 add_sub, 2 sub_add, 3 sub_sub,
        // 4 mul_add, 5 mul_sub, 6 add_mul, 7 sub_mul.
        add(train,0,{
            "parto da 3 aggiungo 4 poi aggiungo 2",
            "ho 5 ricevo 2 e poi ricevo 6",
            "inizio con 7 sommo 3 poi sommo 4",
            "parto da 10 aggiungo 5 e infine aggiungo 1",
            "ho 2 aumento di 8 poi aumento di 3",
            "inizio da 6 ricevo 4 poi ricevo 5"
        });
        add(val,0,{"parto da 4 aggiungo 3 poi aggiungo 7","ho 8 ricevo 2 poi ricevo 1"});
        add(test,0,{"inizio con 9 sommo 4 poi sommo 2","parto da 1 aumento di 5 poi aumento di 6"});

        add(train,1,{
            "parto da 10 aggiungo 3 poi tolgo 4",
            "ho 8 ricevo 5 e poi perdo 2",
            "inizio con 12 sommo 4 poi sottraggo 3",
            "parto da 7 aggiungo 6 e infine tolgo 5",
            "ho 15 aumento di 2 poi diminuisco di 4",
            "inizio da 9 ricevo 3 poi spendo 2"
        });
        add(val,1,{"parto da 11 aggiungo 5 poi tolgo 3","ho 6 ricevo 4 poi perdo 2"});
        add(test,1,{"inizio con 13 sommo 2 poi sottraggo 6","parto da 8 aumento di 7 poi diminuisco di 5"});

        add(train,2,{
            "parto da 10 tolgo 3 poi aggiungo 4",
            "ho 12 perdo 5 e poi ricevo 6",
            "inizio con 15 sottraggo 4 poi sommo 2",
            "parto da 9 tolgo 2 e infine aggiungo 7",
            "ho 14 diminuisco di 6 poi aumento di 3",
            "inizio da 11 spendo 4 poi ricevo 5"
        });
        add(val,2,{"parto da 16 tolgo 5 poi aggiungo 2","ho 13 perdo 3 poi ricevo 4"});
        add(test,2,{"inizio con 18 sottraggo 7 poi sommo 3","parto da 12 diminuisco di 4 poi aumento di 8"});

        add(train,3,{
            "parto da 15 tolgo 4 poi tolgo 3",
            "ho 18 perdo 5 e poi perdo 2",
            "inizio con 20 sottraggo 6 poi sottraggo 4",
            "parto da 12 tolgo 3 e infine tolgo 2",
            "ho 17 diminuisco di 5 poi diminuisco di 4",
            "inizio da 14 spendo 3 poi spendo 2"
        });
        add(val,3,{"parto da 19 tolgo 7 poi tolgo 4","ho 16 perdo 5 poi perdo 3"});
        add(test,3,{"inizio con 21 sottraggo 8 poi sottraggo 2","parto da 13 diminuisco di 4 poi diminuisco di 3"});

        add(train,4,{
            "parto da 3 moltiplico per 4 poi aggiungo 2",
            "ho 5 volte 2 e poi ricevo 3",
            "inizio con 6 moltiplico per 3 poi sommo 4",
            "parto da 2 faccio per 5 e infine aggiungo 7",
            "ho 4 moltiplico per 6 poi aumento di 2",
            "inizio da 7 faccio per 2 poi ricevo 5"
        });
        add(val,4,{"parto da 8 moltiplico per 3 poi aggiungo 4","ho 9 faccio per 2 poi ricevo 1"});
        add(test,4,{"inizio con 5 moltiplico per 4 poi sommo 3","parto da 6 faccio per 3 poi aumento di 2"});

        add(train,5,{
            "parto da 3 moltiplico per 4 poi tolgo 2",
            "ho 5 volte 3 e poi perdo 4",
            "inizio con 6 moltiplico per 2 poi sottraggo 3",
            "parto da 4 faccio per 5 e infine tolgo 6",
            "ho 7 moltiplico per 3 poi diminuisco di 2",
            "inizio da 8 faccio per 2 poi spendo 5"
        });
        add(val,5,{"parto da 9 moltiplico per 2 poi tolgo 4","ho 6 faccio per 4 poi perdo 3"});
        add(test,5,{"inizio con 7 moltiplico per 5 poi sottraggo 6","parto da 8 faccio per 3 poi diminuisco di 4"});

        add(train,6,{
            "parto da 3 aggiungo 2 poi moltiplico tutto per 4",
            "ho 5 ricevo 3 e poi faccio per 2",
            "inizio con 6 sommo 4 poi moltiplico il risultato per 3",
            "parto da 2 aggiungo 7 e infine moltiplico per 5",
            "ho 4 aumento di 3 poi faccio per 6",
            "inizio da 7 ricevo 2 poi moltiplico tutto per 3"
        });
        add(val,6,{"parto da 8 aggiungo 5 poi moltiplico tutto per 2","ho 6 ricevo 3 poi faccio per 4"});
        add(test,6,{"inizio con 5 sommo 7 poi moltiplico il risultato per 3","parto da 9 aumento di 2 poi faccio per 5"});

        add(train,7,{
            "parto da 8 tolgo 2 poi moltiplico tutto per 3",
            "ho 10 perdo 4 e poi faccio per 2",
            "inizio con 12 sottraggo 5 poi moltiplico il risultato per 4",
            "parto da 9 tolgo 3 e infine moltiplico per 5",
            "ho 11 diminuisco di 2 poi faccio per 3",
            "inizio da 14 spendo 4 poi moltiplico tutto per 2"
        });
        add(val,7,{"parto da 13 tolgo 5 poi moltiplico tutto per 3","ho 9 perdo 2 poi faccio per 4"});
        add(test,7,{"inizio con 15 sottraggo 6 poi moltiplico il risultato per 2","parto da 12 diminuisco di 3 poi faccio per 5"});

        std::mt19937 r(seed ^ 0x4A11C0DEu);
        deterministicShuffle(train,r);
        deterministicShuffle(val,r);
        deterministicShuffle(test,r);
    }
};

class Goal4ReasoningBrain {
public:
    static constexpr int FEATURES=512;
    static constexpr int CLASSES=8;
    uint32_t seed=174;
    int step=0;
    std::vector<float> w;
    std::vector<float> b;

    explicit Goal4ReasoningBrain(uint32_t s=174):seed(s),w(FEATURES*CLASSES),b(CLASSES,0.0f){
        std::mt19937 r(seed ^ 0x4A11BEEFu);
        for(float& x:w) x=0.003f*deterministicNormalApprox(r);
    }

    int parameterCount() const { return static_cast<int>(w.size()+b.size()); }

    static std::vector<float> reasoningFeatures(const std::string& text){
        std::istringstream in(normalizeItalian(text));
        std::ostringstream out;
        std::vector<std::string> ops;
        std::string token;
        bool first=true;

        auto pushOp=[&](const std::string& op){
            if(ops.size()<2 && (ops.empty() || ops.back()!=op)) ops.push_back(op);
        };

        while(in>>token){
            bool digits=!token.empty();
            for(char ch:token) if(ch<'0'||ch>'9'){digits=false;break;}

            if(token=="aggiungo" || token=="ricevo" || token=="sommo" || token=="aumento")
                pushOp("add");
            else if(token=="tolgo" || token=="perdo" || token=="sottraggo" || token=="diminuisco" || token=="spendo")
                pushOp("sub");
            else if(token=="moltiplico" || token=="volte" || token=="faccio")
                pushOp("mul");

            if(digits || numberWord(token)>=0) token="numero";
            if(!first) out<<" ";
            first=false;
            out<<token;
        }

        auto x=Goal1IntentBrain::features(out.str());
        auto add=[&](const std::string& key,float weight){
            x[goalHash(key)%FEATURES]+=weight;
        };
        if(!ops.empty()) add("g4:first:"+ops[0],3.0f);
        if(ops.size()>1) add("g4:second:"+ops[1],3.0f);
        if(ops.size()>1) add("g4:pair:"+ops[0]+":"+ops[1],4.0f);

        double sq=0.0;
        for(float v:x) sq+=double(v)*v;
        float inv=sq>0.0?float(1.0/std::sqrt(sq)):1.0f;
        for(float& v:x) v*=inv;
        return x;
    }

    std::vector<float> logits(const std::string& text) const {
        auto x=reasoningFeatures(text);
        std::vector<float> z(CLASSES,0.0f);
        for(int k=0;k<CLASSES;++k){
            float s=b[k];
            for(int i=0;i<FEATURES;++i) s+=x[i]*w[i*CLASSES+k];
            z[k]=s;
        }
        return z;
    }

    int predict(const std::string& text,float* confidence=nullptr) const {
        auto z=logits(text);
        float mx=*std::max_element(z.begin(),z.end());
        float sum=0.0f;
        std::vector<float> p(CLASSES);
        for(int k=0;k<CLASSES;++k){ p[k]=std::exp(z[k]-mx); sum+=p[k]; }
        int best=0; float bp=-1.0f;
        for(int k=0;k<CLASSES;++k){ p[k]/=sum; if(p[k]>bp){bp=p[k];best=k;} }
        if(confidence) *confidence=bp;
        return best;
    }

    Metrics evaluate(const std::vector<Goal4Example>& set) const {
        if(set.empty()) return {};
        double loss=0.0; int correct=0;
        for(const auto& e:set){
            auto z=logits(e.text);
            float mx=*std::max_element(z.begin(),z.end());
            float sum=0.0f; for(float v:z) sum+=std::exp(v-mx);
            loss-=z[e.label]-mx-std::log(sum);
            int best=static_cast<int>(std::max_element(z.begin(),z.end())-z.begin());
            if(best==e.label) ++correct;
        }
        return {static_cast<float>(loss/set.size()),static_cast<float>(correct)/set.size()};
    }

    Goal4TrainResult train(const Goal4Dataset& data,int steps,int batch,float lr){
        auto t0=std::chrono::steady_clock::now();
        int done=0;
        for(int s=0;s<steps;++s){
            std::mt19937 r(seed ^ 0x4A11CAFEu ^ static_cast<uint32_t>(step+1));
            std::vector<float> gw(w.size(),0.0f),gb(b.size(),0.0f);
            for(int n=0;n<batch;++n){
                const auto& e=data.train[deterministicIndex(r,data.train.size())];
                auto x=reasoningFeatures(e.text);
                std::vector<float> z(CLASSES);
                float mx=-std::numeric_limits<float>::infinity();
                for(int k=0;k<CLASSES;++k){
                    float q=b[k];
                    for(int i=0;i<FEATURES;++i) q+=x[i]*w[i*CLASSES+k];
                    z[k]=q; mx=std::max(mx,q);
                }
                float sum=0.0f;
                for(int k=0;k<CLASSES;++k){ z[k]=std::exp(z[k]-mx); sum+=z[k]; }
                for(int k=0;k<CLASSES;++k){
                    float g=z[k]/sum-(k==e.label?1.0f:0.0f);
                    gb[k]+=g;
                    for(int i=0;i<FEATURES;++i) if(x[i]!=0.0f) gw[i*CLASSES+k]+=g*x[i];
                }
            }
            float rate=lr/std::max(1,batch);
            for(size_t i=0;i<w.size();++i) w[i]-=rate*(gw[i]+0.0002f*w[i]);
            for(size_t i=0;i<b.size();++i) b[i]-=rate*gb[i];
            ++step; ++done;
        }
        auto t1=std::chrono::steady_clock::now();
        Goal4TrainResult out;
        out.steps_completed=done;
        out.train=evaluate(data.train);
        out.validation=evaluate(data.val);
        out.elapsed_seconds=std::chrono::duration<double>(t1-t0).count();
        return out;
    }

    static const char* planName(int k){
        static const char* names[CLASSES]={"add_add","add_sub","sub_add","sub_sub","mul_add","mul_sub","add_mul","sub_mul"};
        return (k>=0&&k<CLASSES)?names[k]:"unknown";
    }

    static int numberWord(const std::string& x){
        static const char* names[]={"zero","uno","due","tre","quattro","cinque","sei","sette","otto","nove",
            "dieci","undici","dodici","tredici","quattordici","quindici","sedici","diciassette","diciotto","diciannove","venti"};
        for(int i=0;i<=20;++i) if(x==names[i]) return i;
        return -1;
    }

    static bool extractThree(const std::string& text,int& a,int& b,int& c){
        std::vector<int> nums;
        std::istringstream in(normalizeItalian(text));
        std::string token;
        while(in>>token){
            bool digits=!token.empty();
            for(char ch:token) if(ch<'0'||ch>'9'){digits=false;break;}
            if(digits){
                try{nums.push_back(std::stoi(token));}catch(...){}
            }else{
                int v=numberWord(token);
                if(v>=0) nums.push_back(v);
            }
        }
        if(nums.size()<3) return false;
        a=nums[0];b=nums[1];c=nums[2];
        return true;
    }

    static long long execute(int plan,int a,int b,int c){
        switch(plan){
            case 0:return static_cast<long long>(a)+b+c;
            case 1:return static_cast<long long>(a)+b-c;
            case 2:return static_cast<long long>(a)-b+c;
            case 3:return static_cast<long long>(a)-b-c;
            case 4:return static_cast<long long>(a)*b+c;
            case 5:return static_cast<long long>(a)*b-c;
            case 6:return (static_cast<long long>(a)+b)*c;
            case 7:return (static_cast<long long>(a)-b)*c;
            default:return 0;
        }
    }
};


struct Goal5Example {
    std::string text;
    int label = 0; // 0 local_known, 1 verify
};

struct Goal5Dataset {
    std::vector<Goal5Example> train, val, test;

    static void add(std::vector<Goal5Example>& dst,int label,std::initializer_list<const char*> values){
        for(const char* s:values) dst.push_back({s,label});
    }

    explicit Goal5Dataset(uint32_t seed){
        add(train,0,{
            "ciao","salve","buongiorno","come mi chiamo","qual e il mio nome",
            "dove vivo","qual e la mia citta","qual e il mio colore preferito",
            "che animale ho","parto da 10 aggiungo 3 poi tolgo 4",
            "parto da 6 moltiplico per 4 poi aggiungo 3","ricorda che mi chiamo luca",
            "vivo a roma","il mio colore preferito e blu","ho un cane di nome fido",
            "parto da 12 tolgo 5 poi aggiungo 2"
        });
        add(val,0,{
            "ehi come va","ti ricordi il mio nome","dove avevo detto di abitare",
            "che colore ricordi di me","parto da 8 aggiungo 2 poi moltiplico tutto per 3",
            "ricordati che vivo a torino"
        });
        add(test,0,{
            "buonasera","come avevo detto di chiamarmi","ti ricordi la mia citta",
            "dimmi il colore che preferisco","parto da 9 tolgo 3 poi aggiungo 7",
            "salva che il mio animale e un gatto"
        });

        add(train,1,{
            "chi e dante","quando e nato galileo","qual e la capitale del portogallo",
            "che tempo fa oggi","quali sono le notizie di oggi","cerca informazioni su marte",
            "trova notizie sulla tecnologia","quanto costa oggi il bitcoin",
            "chi ha vinto la partita ieri","qual e il presidente attuale",
            "spiegami una scoperta scientifica recente","cerca sul web la luna",
            "qual e la popolazione di roma","quando parte il prossimo treno",
            "dimmi il prezzo corrente dell oro","chi ha scritto i promessi sposi"
        });
        add(val,1,{
            "qual e la capitale di spagna","che tempo fara domani","trova notizie sull astronomia",
            "quanto vale oggi l euro","chi ha vinto ieri","quando e nato mozart"
        });
        add(test,1,{
            "chi e michelangelo","qual e la capitale della grecia","cerca novita sulla scienza",
            "che temperatura c e adesso","qual e il prezzo attuale del petrolio","quando e nato verdi"
        });

        std::mt19937 r(seed ^ 0x5A11C0DEu);
        deterministicShuffle(train,r);
        deterministicShuffle(val,r);
        deterministicShuffle(test,r);
    }
};

class Goal5UncertaintyBrain {
public:
    static constexpr int FEATURES=512;
    static constexpr int CLASSES=2;
    uint32_t seed=174;
    int step=0;
    std::vector<float> w;
    std::vector<float> b;

    explicit Goal5UncertaintyBrain(uint32_t s=174):seed(s),w(FEATURES*CLASSES),b(CLASSES,0.0f){
        std::mt19937 r(seed ^ 0x5A11BEEFu);
        for(float& x:w) x=0.003f*deterministicNormalApprox(r);
    }

    int parameterCount() const { return static_cast<int>(w.size()+b.size()); }

    std::vector<float> logits(const std::string& text) const {
        auto x=Goal1IntentBrain::features(text);
        std::vector<float> z(CLASSES,0.0f);
        for(int k=0;k<CLASSES;++k){
            float s=b[k];
            for(int i=0;i<FEATURES;++i) s+=x[i]*w[i*CLASSES+k];
            z[k]=s;
        }
        return z;
    }

    int predict(const std::string& text,float* confidence=nullptr) const {
        auto z=logits(text);
        float mx=*std::max_element(z.begin(),z.end());
        float sum=0.0f;
        std::vector<float> p(CLASSES);
        for(int k=0;k<CLASSES;++k){ p[k]=std::exp(z[k]-mx); sum+=p[k]; }
        int best=0; float bp=-1.0f;
        for(int k=0;k<CLASSES;++k){ p[k]/=sum; if(p[k]>bp){bp=p[k];best=k;} }
        if(confidence) *confidence=bp;
        return best;
    }

    Metrics evaluate(const std::vector<Goal5Example>& set) const {
        if(set.empty()) return {};
        double loss=0.0; int correct=0;
        for(const auto& e:set){
            auto z=logits(e.text);
            float mx=*std::max_element(z.begin(),z.end());
            float sum=0.0f; for(float v:z) sum+=std::exp(v-mx);
            loss-=z[e.label]-mx-std::log(sum);
            int best=static_cast<int>(std::max_element(z.begin(),z.end())-z.begin());
            if(best==e.label) ++correct;
        }
        return {static_cast<float>(loss/set.size()),static_cast<float>(correct)/set.size()};
    }

    Goal5TrainResult train(const Goal5Dataset& data,int steps,int batch,float lr){
        auto t0=std::chrono::steady_clock::now();
        int done=0;
        for(int s=0;s<steps;++s){
            std::mt19937 r(seed ^ 0x5A11CAFEu ^ static_cast<uint32_t>(step+1));
            std::vector<float> gw(w.size(),0.0f),gb(b.size(),0.0f);
            for(int n=0;n<batch;++n){
                const auto& e=data.train[deterministicIndex(r,data.train.size())];
                auto x=Goal1IntentBrain::features(e.text);
                std::vector<float> z(CLASSES);
                float mx=-std::numeric_limits<float>::infinity();
                for(int k=0;k<CLASSES;++k){
                    float q=b[k];
                    for(int i=0;i<FEATURES;++i) q+=x[i]*w[i*CLASSES+k];
                    z[k]=q; mx=std::max(mx,q);
                }
                float sum=0.0f;
                for(int k=0;k<CLASSES;++k){ z[k]=std::exp(z[k]-mx); sum+=z[k]; }
                for(int k=0;k<CLASSES;++k){
                    float gg=z[k]/sum-(k==e.label?1.0f:0.0f);
                    gb[k]+=gg;
                    for(int i=0;i<FEATURES;++i) if(x[i]!=0.0f) gw[i*CLASSES+k]+=gg*x[i];
                }
            }
            float rate=lr/std::max(1,batch);
            for(size_t i=0;i<w.size();++i) w[i]-=rate*(gw[i]+0.0002f*w[i]);
            for(size_t i=0;i<b.size();++i) b[i]-=rate*gb[i];
            ++step; ++done;
        }
        auto t1=std::chrono::steady_clock::now();
        Goal5TrainResult out;
        out.steps_completed=done;
        out.train=evaluate(data.train);
        out.validation=evaluate(data.val);
        out.elapsed_seconds=std::chrono::duration<double>(t1-t0).count();
        return out;
    }

    static const char* decisionName(int k){
        return k==0 ? "local_known" : "verify";
    }
};


struct Goal6Example {
    std::string text;
    int label = 0; // 0 wikipedia/static, 1 live
};

struct Goal6Dataset {
    std::vector<Goal6Example> train, val, test;
    static void add(std::vector<Goal6Example>& dst,int label,std::initializer_list<const char*> values){
        for(const char* s:values) dst.push_back({s,label});
    }
    explicit Goal6Dataset(uint32_t seed){
        add(train,0,{
            "chi e dante alighieri","chi era galileo galilei","quando e nato mozart",
            "qual e la capitale del portogallo","spiegami il pianeta marte",
            "cerca informazioni su leonardo da vinci","chi ha scritto i promessi sposi",
            "cos e la fotosintesi","dove si trova il monte everest","quando e nata roma",
            "parlami di raffaello","cerca informazioni sulla rivoluzione francese",
            "qual e la capitale della grecia","chi e marie curie","cos e un vulcano",
            "spiegami il sistema solare"
        });
        add(val,0,{
            "chi e michelangelo","quando e nato verdi","qual e la capitale della spagna",
            "spiegami la luna","cerca informazioni su napoleone","cos e il dna"
        });
        add(test,0,{
            "chi era giotto","quando e nato einstein","qual e la capitale dell austria",
            "parlami di saturno","cerca informazioni su garibaldi","cos e un terremoto"
        });

        add(train,1,{
            "che tempo fa oggi","che temperatura c e adesso","quanto costa oggi il bitcoin",
            "qual e il prezzo corrente dell oro","quali sono le notizie di oggi",
            "chi ha vinto la partita ieri","quando parte il prossimo treno",
            "qual e il presidente attuale","quanto vale oggi l euro",
            "che tempo fara domani","qual e il traffico adesso","quali film sono al cinema oggi",
            "chi sta giocando adesso","prezzo attuale del petrolio","notizie di questa mattina",
            "orari dei treni di oggi"
        });
        add(val,1,{
            "meteo di domani","prezzo del bitcoin adesso","notizie di oggi sulla tecnologia",
            "chi ha vinto ieri","quanto vale l oro oggi","prossimo treno per roma"
        });
        add(test,1,{
            "temperatura adesso a milano","prezzo corrente dell euro","notizie di oggi sul calcio",
            "chi gioca questa sera","traffico attuale a roma","quando parte il prossimo autobus"
        });

        std::mt19937 r(seed ^ 0x6A11C0DEu);
        deterministicShuffle(train,r);
        deterministicShuffle(val,r);
        deterministicShuffle(test,r);
    }
};

class Goal6SearchBrain {
public:
    static constexpr int FEATURES=512;
    static constexpr int CLASSES=2;
    uint32_t seed=174;
    int step=0;
    std::vector<float> w;
    std::vector<float> b;

    explicit Goal6SearchBrain(uint32_t s=174):seed(s),w(FEATURES*CLASSES),b(CLASSES,0.0f){
        std::mt19937 r(seed ^ 0x6A11BEEFu);
        for(float& x:w) x=0.003f*deterministicNormalApprox(r);
    }

    int parameterCount() const { return static_cast<int>(w.size()+b.size()); }

    std::vector<float> logits(const std::string& text) const {
        auto x=Goal1IntentBrain::features(text);
        std::vector<float> z(CLASSES,0.0f);
        for(int k=0;k<CLASSES;++k){
            float s=b[k];
            for(int i=0;i<FEATURES;++i) s+=x[i]*w[i*CLASSES+k];
            z[k]=s;
        }
        return z;
    }

    int predict(const std::string& text,float* confidence=nullptr) const {
        auto z=logits(text);
        float mx=*std::max_element(z.begin(),z.end());
        float sum=0.0f;
        std::vector<float> p(CLASSES);
        for(int k=0;k<CLASSES;++k){p[k]=std::exp(z[k]-mx);sum+=p[k];}
        int best=0; float bp=-1.0f;
        for(int k=0;k<CLASSES;++k){p[k]/=sum;if(p[k]>bp){bp=p[k];best=k;}}
        if(confidence) *confidence=bp;
        return best;
    }

    Metrics evaluate(const std::vector<Goal6Example>& set) const {
        if(set.empty()) return {};
        double loss=0.0; int correct=0;
        for(const auto& e:set){
            auto z=logits(e.text);
            float mx=*std::max_element(z.begin(),z.end());
            float sum=0.0f; for(float v:z) sum+=std::exp(v-mx);
            loss-=z[e.label]-mx-std::log(sum);
            int best=static_cast<int>(std::max_element(z.begin(),z.end())-z.begin());
            if(best==e.label) ++correct;
        }
        return {static_cast<float>(loss/set.size()),static_cast<float>(correct)/set.size()};
    }

    Goal6TrainResult train(const Goal6Dataset& data,int steps,int batch,float lr){
        auto t0=std::chrono::steady_clock::now();
        int done=0;
        for(int s=0;s<steps;++s){
            std::mt19937 r(seed ^ 0x6A11CAFEu ^ static_cast<uint32_t>(step+1));
            std::vector<float> gw(w.size(),0.0f),gb(b.size(),0.0f);
            for(int n=0;n<batch;++n){
                const auto& e=data.train[deterministicIndex(r,data.train.size())];
                auto x=Goal1IntentBrain::features(e.text);
                std::vector<float> z(CLASSES);
                float mx=-std::numeric_limits<float>::infinity();
                for(int k=0;k<CLASSES;++k){
                    float q=b[k];
                    for(int i=0;i<FEATURES;++i) q+=x[i]*w[i*CLASSES+k];
                    z[k]=q; mx=std::max(mx,q);
                }
                float sum=0.0f;
                for(int k=0;k<CLASSES;++k){z[k]=std::exp(z[k]-mx);sum+=z[k];}
                for(int k=0;k<CLASSES;++k){
                    float gg=z[k]/sum-(k==e.label?1.0f:0.0f);
                    gb[k]+=gg;
                    for(int i=0;i<FEATURES;++i) if(x[i]!=0.0f) gw[i*CLASSES+k]+=gg*x[i];
                }
            }
            float rate=lr/std::max(1,batch);
            for(size_t i=0;i<w.size();++i) w[i]-=rate*(gw[i]+0.0002f*w[i]);
            for(size_t i=0;i<b.size();++i) b[i]-=rate*gb[i];
            ++step; ++done;
        }
        auto t1=std::chrono::steady_clock::now();
        Goal6TrainResult out;
        out.steps_completed=done;
        out.train=evaluate(data.train);
        out.validation=evaluate(data.val);
        out.elapsed_seconds=std::chrono::duration<double>(t1-t0).count();
        return out;
    }

    static std::string queryFrom(const std::string& raw){
        std::string s=normalizeItalian(raw);
        static const std::vector<std::string> prefixes={
            "cerca informazioni su ","parlami di ","spiegami ","chi e ","chi era ",
            "cos e ","quando e nato ","quando e nata ","qual e la capitale del ",
            "qual e la capitale della ","qual e la capitale di ","dove si trova "
        };
        for(const auto& p:prefixes){
            if(s.rfind(p,0)==0){s=s.substr(p.size());break;}
        }
        while(!s.empty()&&s.front()==' ') s.erase(s.begin());
        while(!s.empty()&&s.back()==' ') s.pop_back();
        return s;
    }

    static const char* sourceName(int k){ return k==0 ? "wikipedia" : "live"; }
};

Metrics eval(TinyTransformer&m,const std::vector<Example>&set){
    if(set.empty()) return {};
    double loss=0; int correct=0,total=0;
    for(auto&e:set){
        Tensor logits=m.forward(e.x); Tensor L=crossEntropy(logits,e.y); loss+=L.n->data[0];
        int v=logits.dim(1);
        for(int pos=e.answer_start; pos<e.answer_start+e.answer_len; ++pos){
            int best=0; float mx=-1e30f;
            for(int j=0;j<v;++j){ float z=logits.n->data[pos*v+j]; if(z>mx){mx=z;best=j;} }
            if(best==e.y[pos]) ++correct;
            ++total;
        }
    }
    return {(float)(loss/set.size()), total?float(correct)/total:0};
}

std::string esc(const std::string&s){ std::string o; for(char c:s){ if(c=='\\'||c=='\"'){o+='\\';o+=c;} else if(c=='\n')o+="\\n"; else o+=c;} return o; }

} // namespace

class Engine::Impl {
public:
    uint32_t seed=174;
    TinyTransformer model;
    int curriculum=0;
    int curriculum_start_step=0;
    Dataset data;
    int step=0;
    Goal1IntentBrain goal1;
    Goal1Dataset goal1data;
    Goal2ResponseBrain goal2;
    Goal2Dataset goal2data;
    Goal3MemoryBrain goal3;
    Goal3Dataset goal3data;
    Goal4ReasoningBrain goal4;
    Goal4Dataset goal4data;
    Goal5UncertaintyBrain goal5;
    Goal5Dataset goal5data;
    Goal6SearchBrain goal6;
    Goal6Dataset goal6data;
    std::atomic<bool> pause{false};
    mutable std::mutex mu;
    explicit Impl(uint32_t s)
        :seed(s),model(s),curriculum(0),curriculum_start_step(0),data(s,0),
         goal1(s),goal1data(s),goal2(s),goal2data(s),goal3(s),goal3data(s),goal4(s),goal4data(s),goal5(s),goal5data(s),goal6(s),goal6data(s){}
};

Engine::Engine(uint32_t seed):impl_(std::make_unique<Impl>(seed)){}
Engine::~Engine()=default;
void Engine::reset(uint32_t seed){ impl_=std::make_unique<Impl>(seed); }
Metrics Engine::evaluateTrain(){ std::lock_guard<std::mutex> g(impl_->mu); return eval(impl_->model,impl_->data.train); }
Metrics Engine::evaluateValidation(){ std::lock_guard<std::mutex> g(impl_->mu); return eval(impl_->model,impl_->data.val); }
Metrics Engine::evaluateTest(){ std::lock_guard<std::mutex> g(impl_->mu); return eval(impl_->model,impl_->data.test); }
Metrics Engine::evaluateRetentionL0(){ std::lock_guard<std::mutex> g(impl_->mu); return eval(impl_->model,impl_->data.retention_l0); }
Metrics Engine::evaluateRetentionL1(){ std::lock_guard<std::mutex> g(impl_->mu); return eval(impl_->model,impl_->data.retention_l1); }
Metrics Engine::evaluateRetentionL2(){ std::lock_guard<std::mutex> g(impl_->mu); return eval(impl_->model,impl_->data.retention_l2); }
Metrics Engine::evaluateRetentionL3(){ std::lock_guard<std::mutex> g(impl_->mu); return eval(impl_->model,impl_->data.retention_l3); }
Metrics Engine::evaluateRetentionL4(){ std::lock_guard<std::mutex> g(impl_->mu); return eval(impl_->model,impl_->data.retention_l4); }
int Engine::parameterCount() const{return impl_->model.parameterCount();}
int Engine::globalStep() const{return impl_->step;}
void Engine::setCurriculum(int level){
    std::lock_guard<std::mutex> g(impl_->mu);
    level=std::max(0,std::min(5,level));
    if(impl_->curriculum==level)return;
    if(level==5 && impl_->curriculum<5) impl_->model.prepareLevel5FromLevel4();
    impl_->curriculum=level;
    impl_->curriculum_start_step=impl_->step;
    impl_->model.resetOptimizerMoments();
    impl_->data=Dataset(impl_->seed,level);
}
int Engine::curriculumLevel() const{ std::lock_guard<std::mutex> g(impl_->mu); return impl_->curriculum; }
int Engine::curriculumStartStep() const{ std::lock_guard<std::mutex> g(impl_->mu); return impl_->curriculum_start_step; }

Metrics Engine::evaluateGoal1Train(){ std::lock_guard<std::mutex> g(impl_->mu); return impl_->goal1.evaluate(impl_->goal1data.train); }
Metrics Engine::evaluateGoal1Validation(){ std::lock_guard<std::mutex> g(impl_->mu); return impl_->goal1.evaluate(impl_->goal1data.val); }
Metrics Engine::evaluateGoal1Test(){ std::lock_guard<std::mutex> g(impl_->mu); return impl_->goal1.evaluate(impl_->goal1data.test); }
Goal1TrainResult Engine::trainGoal1(int steps,int batch,float lr){
    std::lock_guard<std::mutex> g(impl_->mu);
    return impl_->goal1.train(impl_->goal1data,steps,batch,lr);
}
int Engine::goal1Step() const { std::lock_guard<std::mutex> g(impl_->mu); return impl_->goal1.step; }
int Engine::goal1ParameterCount() const { std::lock_guard<std::mutex> g(impl_->mu); return impl_->goal1.parameterCount(); }
std::string Engine::classifyGoal1(const std::string& text) const {
    std::lock_guard<std::mutex> g(impl_->mu);
    float confidence=0.0f;
    int label=impl_->goal1.predict(text,&confidence);
    std::ostringstream s;
    s<<std::fixed<<std::setprecision(4)
     <<"{\"intent\":\""<<Goal1IntentBrain::labelName(label)<<"\",\"confidence\":"<<confidence<<"}";
    return s.str();
}

Metrics Engine::evaluateGoal2Train(){ std::lock_guard<std::mutex> g(impl_->mu); return impl_->goal2.evaluate(impl_->goal2data.train); }
Metrics Engine::evaluateGoal2Validation(){ std::lock_guard<std::mutex> g(impl_->mu); return impl_->goal2.evaluate(impl_->goal2data.val); }
Metrics Engine::evaluateGoal2Test(){ std::lock_guard<std::mutex> g(impl_->mu); return impl_->goal2.evaluate(impl_->goal2data.test); }
Goal2TrainResult Engine::trainGoal2(int steps,int batch,float lr){
    std::lock_guard<std::mutex> g(impl_->mu);
    return impl_->goal2.train(impl_->goal2data,steps,batch,lr);
}
int Engine::goal2Step() const { std::lock_guard<std::mutex> g(impl_->mu); return impl_->goal2.step; }
int Engine::goal2ParameterCount() const { std::lock_guard<std::mutex> g(impl_->mu); return impl_->goal2.parameterCount(); }
std::string Engine::respondGoal2(const std::string& text) const {
    std::lock_guard<std::mutex> g(impl_->mu);
    int intent=impl_->goal1.predict(text,nullptr);
    std::string reply=impl_->goal2.generate(intent);
    std::ostringstream s;
    s<<"{\"intent\":\""<<Goal1IntentBrain::labelName(intent)
     <<"\",\"reply\":\""<<esc(reply)<<"\"}";
    return s.str();
}

Metrics Engine::evaluateGoal3Train(){ std::lock_guard<std::mutex> g(impl_->mu); return impl_->goal3.evaluate(impl_->goal3data.train); }
Metrics Engine::evaluateGoal3Validation(){ std::lock_guard<std::mutex> g(impl_->mu); return impl_->goal3.evaluate(impl_->goal3data.val); }
Metrics Engine::evaluateGoal3Test(){ std::lock_guard<std::mutex> g(impl_->mu); return impl_->goal3.evaluate(impl_->goal3data.test); }
Goal3TrainResult Engine::trainGoal3(int steps,int batch,float lr){
    std::lock_guard<std::mutex> g(impl_->mu);
    return impl_->goal3.train(impl_->goal3data,steps,batch,lr);
}
int Engine::goal3Step() const { std::lock_guard<std::mutex> g(impl_->mu); return impl_->goal3.step; }
int Engine::goal3ParameterCount() const { std::lock_guard<std::mutex> g(impl_->mu); return impl_->goal3.parameterCount(); }
std::string Engine::classifyGoal3(const std::string& text) const {
    std::lock_guard<std::mutex> g(impl_->mu);
    float confidence=0.0f;
    int label=impl_->goal3.predict(text,&confidence);
    std::ostringstream s;
    s<<std::fixed<<std::setprecision(4)
     <<"{\"action\":\""<<Goal3MemoryBrain::actionName(label)
     <<"\",\"slot\":\""<<Goal3MemoryBrain::slotName(label)
     <<"\",\"confidence\":"<<confidence<<"}";
    return s.str();
}

Metrics Engine::evaluateGoal4Train(){ std::lock_guard<std::mutex> g(impl_->mu); return impl_->goal4.evaluate(impl_->goal4data.train); }
Metrics Engine::evaluateGoal4Validation(){ std::lock_guard<std::mutex> g(impl_->mu); return impl_->goal4.evaluate(impl_->goal4data.val); }
Metrics Engine::evaluateGoal4Test(){ std::lock_guard<std::mutex> g(impl_->mu); return impl_->goal4.evaluate(impl_->goal4data.test); }
Goal4TrainResult Engine::trainGoal4(int steps,int batch,float lr){
    std::lock_guard<std::mutex> g(impl_->mu);
    return impl_->goal4.train(impl_->goal4data,steps,batch,lr);
}
int Engine::goal4Step() const { std::lock_guard<std::mutex> g(impl_->mu); return impl_->goal4.step; }
int Engine::goal4ParameterCount() const { std::lock_guard<std::mutex> g(impl_->mu); return impl_->goal4.parameterCount(); }
std::string Engine::solveGoal4(const std::string& text) const {
    std::lock_guard<std::mutex> g(impl_->mu);
    float confidence=0.0f;
    int plan=impl_->goal4.predict(text,&confidence);
    int a=0,b=0,c3=0;
    bool valid=Goal4ReasoningBrain::extractThree(text,a,b,c3);
    std::ostringstream s;
    s<<std::fixed<<std::setprecision(4)
     <<"{\"valid\":"<<(valid?"true":"false")
     <<",\"plan\":\""<<Goal4ReasoningBrain::planName(plan)
     <<"\",\"confidence\":"<<confidence;
    if(valid){
        s<<",\"a\":"<<a<<",\"b\":"<<b<<",\"c\":"<<c3
         <<",\"result\":"<<Goal4ReasoningBrain::execute(plan,a,b,c3);
    }
    s<<"}";
    return s.str();
}

Metrics Engine::evaluateGoal5Train(){ std::lock_guard<std::mutex> g(impl_->mu); return impl_->goal5.evaluate(impl_->goal5data.train); }
Metrics Engine::evaluateGoal5Validation(){ std::lock_guard<std::mutex> g(impl_->mu); return impl_->goal5.evaluate(impl_->goal5data.val); }
Metrics Engine::evaluateGoal5Test(){ std::lock_guard<std::mutex> g(impl_->mu); return impl_->goal5.evaluate(impl_->goal5data.test); }
Goal5TrainResult Engine::trainGoal5(int steps,int batch,float lr){
    std::lock_guard<std::mutex> g(impl_->mu);
    return impl_->goal5.train(impl_->goal5data,steps,batch,lr);
}
int Engine::goal5Step() const { std::lock_guard<std::mutex> g(impl_->mu); return impl_->goal5.step; }
int Engine::goal5ParameterCount() const { std::lock_guard<std::mutex> g(impl_->mu); return impl_->goal5.parameterCount(); }
std::string Engine::classifyGoal5(const std::string& text) const {
    std::lock_guard<std::mutex> g(impl_->mu);
    float confidence=0.0f;
    int label=impl_->goal5.predict(text,&confidence);
    std::ostringstream s;
    s<<std::fixed<<std::setprecision(4)
     <<"{\"decision\":\""<<Goal5UncertaintyBrain::decisionName(label)
     <<"\",\"confidence\":"<<confidence<<"}";
    return s.str();
}

Metrics Engine::evaluateGoal6Train(){ std::lock_guard<std::mutex> g(impl_->mu); return impl_->goal6.evaluate(impl_->goal6data.train); }
Metrics Engine::evaluateGoal6Validation(){ std::lock_guard<std::mutex> g(impl_->mu); return impl_->goal6.evaluate(impl_->goal6data.val); }
Metrics Engine::evaluateGoal6Test(){ std::lock_guard<std::mutex> g(impl_->mu); return impl_->goal6.evaluate(impl_->goal6data.test); }
Goal6TrainResult Engine::trainGoal6(int steps,int batch,float lr){
    std::lock_guard<std::mutex> g(impl_->mu);
    return impl_->goal6.train(impl_->goal6data,steps,batch,lr);
}
int Engine::goal6Step() const { std::lock_guard<std::mutex> g(impl_->mu); return impl_->goal6.step; }
int Engine::goal6ParameterCount() const { std::lock_guard<std::mutex> g(impl_->mu); return impl_->goal6.parameterCount(); }
std::string Engine::planGoal6(const std::string& text) const {
    std::lock_guard<std::mutex> g(impl_->mu);
    float confidence=0.0f;
    int label=impl_->goal6.predict(text,&confidence);
    std::ostringstream s;
    s<<std::fixed<<std::setprecision(4)
     <<"{\"source\":\""<<Goal6SearchBrain::sourceName(label)
     <<"\",\"confidence\":"<<confidence
     <<",\"query\":\""<<esc(Goal6SearchBrain::queryFrom(text))<<"\"}";
    return s.str();
}

void Engine::requestPause(){impl_->pause.store(true);} void Engine::clearPause(){impl_->pause.store(false);}

TrainResult Engine::train(int steps,int batch,float lr){
    std::lock_guard<std::mutex> guard(impl_->mu);
    clearPause();
    auto t0=std::chrono::steady_clock::now();
    std::mt19937 r(impl_->seed+1+impl_->step);
    int done=0;
    float effective_lr=(impl_->curriculum>=5)?lr:((impl_->curriculum>=4)?lr:((impl_->curriculum>=3)?lr*0.65f:((impl_->curriculum>=2)?lr*0.75f:lr)));

    for(int s=0;s<steps;++s){
        if(impl_->pause.load()) break;
        impl_->model.zeroGrad();

        for(int b=0;b<batch;++b){
            size_t idx=0;
            if(impl_->curriculum==1 && impl_->data.train.size()>=96){
                // 50% nuovo L1 + 50% replay L0.
                if(b < batch/2) idx=deterministicIndex(r,48);
                else idx=48+deterministicIndex(r,48);
            }else if(impl_->curriculum==2 && impl_->data.train.size()>=192){
                // L2 50%, replay L1 25%, replay L0 25%.
                if(b < batch/2) idx=deterministicIndex(r,72);
                else if(b < (batch*3)/4) idx=72+deterministicIndex(r,48);
                else idx=120+deterministicIndex(r,72);
            }else if(impl_->curriculum==3 && impl_->data.train.size()>=240){
                // L3 50%, L2/L1/L0 ciascuno circa 1/6.
                if(b < batch/2) idx=deterministicIndex(r,72);
                else if(b < 16) idx=72+deterministicIndex(r,48);
                else if(b < 20) idx=120+deterministicIndex(r,48);
                else idx=168+deterministicIndex(r,72);
            }else if(impl_->curriculum==4 && impl_->data.train.size()>=1308){
                // L4 50%; replay L3/L2/L1/L0 12.5% ciascuno.
                if(b < 12) idx=deterministicIndex(r,1092);
                else if(b < 15) idx=1092+deterministicIndex(r,48);
                else if(b < 18) idx=1140+deterministicIndex(r,48);
                else if(b < 21) idx=1188+deterministicIndex(r,48);
                else idx=1236+deterministicIndex(r,72);
            }else if(impl_->curriculum>=5 && impl_->data.train.size()>=456){
                // L5 modular and isolated: 100% new-task batches.
                idx=deterministicIndex(r,80);
            }else{
                idx=deterministicIndex(r,impl_->data.train.size());
            }

            auto&e=impl_->data.train[idx];
            Tensor L=(impl_->curriculum<=0)
                ?impl_->model.loss(e.x,e.y)
                :impl_->model.lossRange(e.x,e.y,e.answer_start,e.answer_len+1);
            backward(L);
        }

        ++impl_->step;
        impl_->model.adamStep(effective_lr,batch,impl_->step,impl_->curriculum);
        ++done;
    }

    auto t1=std::chrono::steady_clock::now();
    TrainResult tr;
    tr.steps_completed=done;
    tr.train=eval(impl_->model,impl_->data.train);
    tr.validation=eval(impl_->model,impl_->data.val);
    // TEST volutamente non consultato durante il training.
    tr.retention_l0=eval(impl_->model,impl_->data.retention_l0);
    tr.retention_l1=eval(impl_->model,impl_->data.retention_l1);
    tr.retention_l2=eval(impl_->model,impl_->data.retention_l2);
    tr.retention_l3=eval(impl_->model,impl_->data.retention_l3);
    tr.retention_l4=eval(impl_->model,impl_->data.retention_l4);
    tr.elapsed_seconds=std::chrono::duration<double>(t1-t0).count();
    tr.paused=impl_->pause.load();
    return tr;
}

std::string Engine::generate(const std::string&prefix,int new_chars){
    std::lock_guard<std::mutex> guard(impl_->mu);
    std::vector<int> ids; for(char c:prefix){ int k=Dataset::id(c); if(k<0)return "[carattere non supportato]"; ids.push_back(k);} if(ids.empty())return "";
    std::string out=prefix;
    for(int n=0;n<new_chars && (int)ids.size()<impl_->model.context;++n){ Tensor l=impl_->model.forward(ids); int t=l.dim(0),v=l.dim(1),best=0;float mx=-1e30f;for(int j=0;j<v;++j){float z=l.n->data[(t-1)*v+j];if(z>mx){mx=z;best=j;}} ids.push_back(best);out+=Dataset::ch(best);if(best==0)break; }
    return out;
}

bool Engine::saveCheckpoint(const std::string&dir) const{
    std::lock_guard<std::mutex> guard(impl_->mu);
    try{
        std::filesystem::create_directories(dir);
        std::ofstream w(dir+"/weights.bin",std::ios::binary); if(!w)return false;
        const char magic[8]={'M','O','T','A','I','0','1','7'}; w.write(magic,8);
        uint32_t ver=11,seed=impl_->seed,step=impl_->step,level=impl_->curriculum,start_step=impl_->curriculum_start_step,pc=impl_->model.p.size();
        w.write((char*)&ver,4); w.write((char*)&seed,4); w.write((char*)&step,4); w.write((char*)&level,4); w.write((char*)&start_step,4); w.write((char*)&pc,4);
        for(auto&z:impl_->model.p){
            uint32_t nl=z.name.size(),sz=z.value.n->data.size();
            w.write((char*)&nl,4); w.write(z.name.data(),nl); w.write((char*)&sz,4);
            w.write((char*)z.value.n->data.data(),sz*sizeof(float));
            w.write((char*)z.m.data(),sz*sizeof(float)); w.write((char*)z.v.data(),sz*sizeof(float));
        }
        uint32_t goal1_step=static_cast<uint32_t>(impl_->goal1.step);
        uint32_t goal1_w=static_cast<uint32_t>(impl_->goal1.w.size());
        uint32_t goal1_b=static_cast<uint32_t>(impl_->goal1.b.size());
        w.write((char*)&goal1_step,4); w.write((char*)&goal1_w,4); w.write((char*)&goal1_b,4);
        w.write((char*)impl_->goal1.w.data(),goal1_w*sizeof(float));
        w.write((char*)impl_->goal1.b.data(),goal1_b*sizeof(float));
        uint32_t goal2_step=static_cast<uint32_t>(impl_->goal2.step);
        uint32_t goal2_w=static_cast<uint32_t>(impl_->goal2.w.size());
        w.write((char*)&goal2_step,4); w.write((char*)&goal2_w,4);
        w.write((char*)impl_->goal2.w.data(),goal2_w*sizeof(float));
        uint32_t goal3_step=static_cast<uint32_t>(impl_->goal3.step);
        uint32_t goal3_w=static_cast<uint32_t>(impl_->goal3.w.size());
        uint32_t goal3_b=static_cast<uint32_t>(impl_->goal3.b.size());
        w.write((char*)&goal3_step,4); w.write((char*)&goal3_w,4); w.write((char*)&goal3_b,4);
        w.write((char*)impl_->goal3.w.data(),goal3_w*sizeof(float));
        w.write((char*)impl_->goal3.b.data(),goal3_b*sizeof(float));
        uint32_t goal4_step=static_cast<uint32_t>(impl_->goal4.step);
        uint32_t goal4_w=static_cast<uint32_t>(impl_->goal4.w.size());
        uint32_t goal4_b=static_cast<uint32_t>(impl_->goal4.b.size());
        w.write((char*)&goal4_step,4); w.write((char*)&goal4_w,4); w.write((char*)&goal4_b,4);
        w.write((char*)impl_->goal4.w.data(),goal4_w*sizeof(float));
        w.write((char*)impl_->goal4.b.data(),goal4_b*sizeof(float));
        uint32_t goal5_step=static_cast<uint32_t>(impl_->goal5.step);
        uint32_t goal5_w=static_cast<uint32_t>(impl_->goal5.w.size());
        uint32_t goal5_b=static_cast<uint32_t>(impl_->goal5.b.size());
        w.write((char*)&goal5_step,4); w.write((char*)&goal5_w,4); w.write((char*)&goal5_b,4);
        w.write((char*)impl_->goal5.w.data(),goal5_w*sizeof(float));
        w.write((char*)impl_->goal5.b.data(),goal5_b*sizeof(float));
        uint32_t goal6_step=static_cast<uint32_t>(impl_->goal6.step);
        uint32_t goal6_w=static_cast<uint32_t>(impl_->goal6.w.size());
        uint32_t goal6_b=static_cast<uint32_t>(impl_->goal6.b.size());
        w.write((char*)&goal6_step,4); w.write((char*)&goal6_w,4); w.write((char*)&goal6_b,4);
        w.write((char*)impl_->goal6.w.data(),goal6_w*sizeof(float));
        w.write((char*)impl_->goal6.b.data(),goal6_b*sizeof(float));
        w.close();
        Metrics va=eval(const_cast<TinyTransformer&>(impl_->model),impl_->data.val);
        Metrics r0=eval(const_cast<TinyTransformer&>(impl_->model),impl_->data.retention_l0);
        Metrics r1=eval(const_cast<TinyTransformer&>(impl_->model),impl_->data.retention_l1);
        Metrics r2=eval(const_cast<TinyTransformer&>(impl_->model),impl_->data.retention_l2);
        Metrics r3=eval(const_cast<TinyTransformer&>(impl_->model),impl_->data.retention_l3);
        Metrics r4=eval(const_cast<TinyTransformer&>(impl_->model),impl_->data.retention_l4);
        std::ofstream j(dir+"/checkpoint.json");
        j<<"{\n  \"format\": \"MOTORAI_CHECKPOINT_NATIVE_V2\",\n  \"seed\": "<<impl_->seed
         <<",\n  \"global_step\": "<<impl_->step<<",\n  \"curriculum_level\": "<<impl_->curriculum
         <<",\n  \"parameter_count\": "<<impl_->model.parameterCount()
         <<",\n  \"validation_loss\": "<<va.loss<<",\n  \"validation_answer_accuracy\": "<<va.answer_accuracy
         <<",\n  \"retention_l0_accuracy\": "<<r0.answer_accuracy
         <<",\n  \"retention_l1_accuracy\": "<<r1.answer_accuracy
         <<",\n  \"retention_l2_accuracy\": "<<r2.answer_accuracy
         <<",\n  \"retention_l3_accuracy\": "<<r3.answer_accuracy
         <<",\n  \"retention_l4_accuracy\": "<<r4.answer_accuracy
         <<",\n  \"goal1_step\": "<<impl_->goal1.step
         <<",\n  \"goal1_validation_accuracy\": "<<impl_->goal1.evaluate(impl_->goal1data.val).answer_accuracy
         <<",\n  \"goal2_step\": "<<impl_->goal2.step
         <<",\n  \"goal2_validation_accuracy\": "<<impl_->goal2.evaluate(impl_->goal2data.val).answer_accuracy
         <<",\n  \"goal3_step\": "<<impl_->goal3.step
         <<",\n  \"goal3_validation_accuracy\": "<<impl_->goal3.evaluate(impl_->goal3data.val).answer_accuracy
         <<",\n  \"goal4_step\": "<<impl_->goal4.step
         <<",\n  \"goal4_validation_accuracy\": "<<impl_->goal4.evaluate(impl_->goal4data.val).answer_accuracy
         <<",\n  \"goal5_step\": "<<impl_->goal5.step
         <<",\n  \"goal5_validation_accuracy\": "<<impl_->goal5.evaluate(impl_->goal5data.val).answer_accuracy
         <<",\n  \"goal6_step\": "<<impl_->goal6.step
         <<",\n  \"goal6_validation_accuracy\": "<<impl_->goal6.evaluate(impl_->goal6data.val).answer_accuracy
         <<",\n  \"pretrained_model\": false,\n  \"weights_origin\": \"random_then_local_training\"\n}\n";
        return (bool)j;
    }catch(...){return false;}
}

bool Engine::loadCheckpoint(const std::string&dir){
    std::lock_guard<std::mutex> guard(impl_->mu);
    try{
        std::ifstream w(dir+"/weights.bin",std::ios::binary); if(!w)return false;
        char magic[8]; w.read(magic,8); std::string m(magic,8);
        uint32_t ver=0,seed=0,step=0,level=0,start_step=0,pc=0;
        if(m=="MOTAI004"){
            w.read((char*)&ver,4); w.read((char*)&seed,4); w.read((char*)&step,4); w.read((char*)&pc,4);
            if(ver!=1) return false;
            level=0;
        }else if(m=="MOTAI005" || m=="MOTAI006" || m=="MOTAI007"){
            w.read((char*)&ver,4); w.read((char*)&seed,4); w.read((char*)&step,4); w.read((char*)&level,4); w.read((char*)&pc,4);
            if(ver!=2 || level>2) return false;
            start_step=(level<=0)?0:((level==1)?220:620);
        }else if(m=="MOTAI008" || m=="MOTAI009"){
            w.read((char*)&ver,4); w.read((char*)&seed,4); w.read((char*)&step,4); w.read((char*)&level,4); w.read((char*)&start_step,4); w.read((char*)&pc,4);
            if(ver!=3 || level>3 || start_step>step) return false;
        }else if(m=="MOTAI010"){
            w.read((char*)&ver,4); w.read((char*)&seed,4); w.read((char*)&step,4); w.read((char*)&level,4); w.read((char*)&start_step,4); w.read((char*)&pc,4);
            if(ver!=4 || level>4 || start_step>step) return false;
        }else if(m=="MOTAI011"){
            w.read((char*)&ver,4); w.read((char*)&seed,4); w.read((char*)&step,4); w.read((char*)&level,4); w.read((char*)&start_step,4); w.read((char*)&pc,4);
            if(ver!=5 || level>5 || start_step>step) return false;
        }else if(m=="MOTAI012"){
            w.read((char*)&ver,4); w.read((char*)&seed,4); w.read((char*)&step,4); w.read((char*)&level,4); w.read((char*)&start_step,4); w.read((char*)&pc,4);
            if(ver!=6 || level>5 || start_step>step) return false;
        }else if(m=="MOTAI013"){
            w.read((char*)&ver,4); w.read((char*)&seed,4); w.read((char*)&step,4); w.read((char*)&level,4); w.read((char*)&start_step,4); w.read((char*)&pc,4);
            if(ver!=7 || level>5 || start_step>step) return false;
        }else if(m=="MOTAI014"){
            w.read((char*)&ver,4); w.read((char*)&seed,4); w.read((char*)&step,4); w.read((char*)&level,4); w.read((char*)&start_step,4); w.read((char*)&pc,4);
            if(ver!=8 || level>5 || start_step>step) return false;
        }else if(m=="MOTAI015"){
            w.read((char*)&ver,4); w.read((char*)&seed,4); w.read((char*)&step,4); w.read((char*)&level,4); w.read((char*)&start_step,4); w.read((char*)&pc,4);
            if(ver!=9 || level>5 || start_step>step) return false;
        }else if(m=="MOTAI016"){
            w.read((char*)&ver,4); w.read((char*)&seed,4); w.read((char*)&step,4); w.read((char*)&level,4); w.read((char*)&start_step,4); w.read((char*)&pc,4);
            if(ver!=10 || level>5 || start_step>step) return false;
        }else if(m=="MOTAI017"){
            w.read((char*)&ver,4); w.read((char*)&seed,4); w.read((char*)&step,4); w.read((char*)&level,4); w.read((char*)&start_step,4); w.read((char*)&pc,4);
            if(ver!=11 || level>5 || start_step>step) return false;
        }else return false;

        // Migrazione per vocabolario 11 -> 12: carica per nome e conserva il nuovo token ? inizializzato localmente.
        for(uint32_t k=0;k<pc;++k){
            uint32_t nl=0,sz=0; w.read((char*)&nl,4);
            std::string name(nl,' '); w.read(name.data(),nl); w.read((char*)&sz,4);
            std::vector<float> data(sz),mm(sz),vv(sz);
            w.read((char*)data.data(),sz*sizeof(float));
            w.read((char*)mm.data(),sz*sizeof(float));
            w.read((char*)vv.data(),sz*sizeof(float));
            auto it=std::find_if(impl_->model.p.begin(),impl_->model.p.end(),[&](const Param& z){return z.name==name;});
            if(it==impl_->model.p.end()) return false;
            size_t target=it->value.n->data.size();
            if(target==sz){
                it->value.n->data=data; it->m=mm; it->v=vv;
            }else if(name=="token" && sz%32u==0 && target==static_cast<size_t>(impl_->model.vocab*32)){
                size_t src_vocab=sz/32u;
                size_t rows=std::min(src_vocab,static_cast<size_t>(impl_->model.vocab));
                for(size_t row=0;row<rows;++row) for(size_t col=0;col<32;++col){
                    size_t src=row*32+col,dst=row*32+col;
                    it->value.n->data[dst]=data[src]; it->m[dst]=mm[src]; it->v[dst]=vv[src];
                }
            }else if(name=="head.w" && sz%32u==0 && target==static_cast<size_t>(32*impl_->model.vocab)){
                size_t src_vocab=sz/32u;
                size_t cols=std::min(src_vocab,static_cast<size_t>(impl_->model.vocab));
                for(size_t row=0;row<32;++row) for(size_t col=0;col<cols;++col){
                    size_t src=row*src_vocab+col,dst=row*static_cast<size_t>(impl_->model.vocab)+col;
                    it->value.n->data[dst]=data[src]; it->m[dst]=mm[src]; it->v[dst]=vv[src];
                }
            }else return false;
        }
        if(!w)return false;
        Goal1IntentBrain loadedGoal(seed);
        if(ver>=6){
            uint32_t goal1_step=0,goal1_w=0,goal1_b=0;
            w.read((char*)&goal1_step,4); w.read((char*)&goal1_w,4); w.read((char*)&goal1_b,4);
            if(goal1_w!=loadedGoal.w.size() || goal1_b!=loadedGoal.b.size()) return false;
            w.read((char*)loadedGoal.w.data(),goal1_w*sizeof(float));
            w.read((char*)loadedGoal.b.data(),goal1_b*sizeof(float));
            if(!w) return false;
            loadedGoal.step=static_cast<int>(goal1_step);
        }
        Goal2ResponseBrain loadedGoal2(seed);
        if(ver>=7){
            uint32_t goal2_step=0,goal2_w=0;
            w.read((char*)&goal2_step,4); w.read((char*)&goal2_w,4);
            if(goal2_w!=loadedGoal2.w.size()) return false;
            w.read((char*)loadedGoal2.w.data(),goal2_w*sizeof(float));
            if(!w) return false;
            loadedGoal2.step=static_cast<int>(goal2_step);
        }
        Goal3MemoryBrain loadedGoal3(seed);
        if(ver>=8){
            uint32_t goal3_step=0,goal3_w=0,goal3_b=0;
            w.read((char*)&goal3_step,4); w.read((char*)&goal3_w,4); w.read((char*)&goal3_b,4);
            if(goal3_w!=loadedGoal3.w.size() || goal3_b!=loadedGoal3.b.size()) return false;
            w.read((char*)loadedGoal3.w.data(),goal3_w*sizeof(float));
            w.read((char*)loadedGoal3.b.data(),goal3_b*sizeof(float));
            if(!w) return false;
            loadedGoal3.step=static_cast<int>(goal3_step);
        }
        Goal4ReasoningBrain loadedGoal4(seed);
        if(ver>=9){
            uint32_t goal4_step=0,goal4_w=0,goal4_b=0;
            w.read((char*)&goal4_step,4); w.read((char*)&goal4_w,4); w.read((char*)&goal4_b,4);
            if(goal4_w!=loadedGoal4.w.size() || goal4_b!=loadedGoal4.b.size()) return false;
            w.read((char*)loadedGoal4.w.data(),goal4_w*sizeof(float));
            w.read((char*)loadedGoal4.b.data(),goal4_b*sizeof(float));
            if(!w) return false;
            loadedGoal4.step=static_cast<int>(goal4_step);
        }
        Goal5UncertaintyBrain loadedGoal5(seed);
        if(ver>=10){
            uint32_t goal5_step=0,goal5_w=0,goal5_b=0;
            w.read((char*)&goal5_step,4); w.read((char*)&goal5_w,4); w.read((char*)&goal5_b,4);
            if(goal5_w!=loadedGoal5.w.size() || goal5_b!=loadedGoal5.b.size()) return false;
            w.read((char*)loadedGoal5.w.data(),goal5_w*sizeof(float));
            w.read((char*)loadedGoal5.b.data(),goal5_b*sizeof(float));
            if(!w) return false;
            loadedGoal5.step=static_cast<int>(goal5_step);
        }
        Goal6SearchBrain loadedGoal6(seed);
        if(ver>=11){
            uint32_t goal6_step=0,goal6_w=0,goal6_b=0;
            w.read((char*)&goal6_step,4); w.read((char*)&goal6_w,4); w.read((char*)&goal6_b,4);
            if(goal6_w!=loadedGoal6.w.size() || goal6_b!=loadedGoal6.b.size()) return false;
            w.read((char*)loadedGoal6.w.data(),goal6_w*sizeof(float));
            w.read((char*)loadedGoal6.b.data(),goal6_b*sizeof(float));
            if(!w) return false;
            loadedGoal6.step=static_cast<int>(goal6_step);
        }
        impl_->seed=seed;
        impl_->curriculum=static_cast<int>(level);
        impl_->curriculum_start_step=static_cast<int>(start_step);
        impl_->data=Dataset(seed,impl_->curriculum);
        impl_->step=step;
        impl_->goal1=std::move(loadedGoal);
        impl_->goal1data=Goal1Dataset(seed);
        impl_->goal2=std::move(loadedGoal2);
        impl_->goal2data=Goal2Dataset(seed);
        impl_->goal3=std::move(loadedGoal3);
        impl_->goal3data=Goal3Dataset(seed);
        impl_->goal4=std::move(loadedGoal4);
        impl_->goal4data=Goal4Dataset(seed);
        impl_->goal5=std::move(loadedGoal5);
        impl_->goal5data=Goal5Dataset(seed);
        impl_->goal6=std::move(loadedGoal6);
        impl_->goal6data=Goal6Dataset(seed);
        return true;
    }catch(...){return false;}
}

std::string Engine::statusJson() const{
    std::lock_guard<std::mutex> guard(impl_->mu);
    Metrics te=eval(impl_->model,impl_->data.test), r0=eval(impl_->model,impl_->data.retention_l0), r1=eval(impl_->model,impl_->data.retention_l1), r2=eval(impl_->model,impl_->data.retention_l2), r3=eval(impl_->model,impl_->data.retention_l3), r4=eval(impl_->model,impl_->data.retention_l4);
    std::ostringstream s; s<<std::fixed<<std::setprecision(4)
      <<"{\"seed\":"<<impl_->seed<<",\"step\":"<<impl_->step<<",\"curriculum\":"<<impl_->curriculum<<",\"curriculum_start_step\":"<<impl_->curriculum_start_step
      <<",\"parameters\":"<<parameterCount()<<",\"test_loss\":"<<te.loss<<",\"test_accuracy\":"<<te.answer_accuracy
      <<",\"retention_l0_accuracy\":"<<r0.answer_accuracy<<",\"retention_l1_accuracy\":"<<r1.answer_accuracy<<",\"retention_l2_accuracy\":"<<r2.answer_accuracy<<",\"retention_l3_accuracy\":"<<r3.answer_accuracy<<",\"retention_l4_accuracy\":"<<r4.answer_accuracy
      <<",\"goal1_step\":"<<impl_->goal1.step<<",\"goal1_validation_accuracy\":"<<impl_->goal1.evaluate(impl_->goal1data.val).answer_accuracy
      <<",\"goal2_step\":"<<impl_->goal2.step<<",\"goal2_validation_accuracy\":"<<impl_->goal2.evaluate(impl_->goal2data.val).answer_accuracy
      <<",\"goal3_step\":"<<impl_->goal3.step<<",\"goal3_validation_accuracy\":"<<impl_->goal3.evaluate(impl_->goal3data.val).answer_accuracy
      <<",\"goal4_step\":"<<impl_->goal4.step<<",\"goal4_validation_accuracy\":"<<impl_->goal4.evaluate(impl_->goal4data.val).answer_accuracy
      <<",\"goal5_step\":"<<impl_->goal5.step<<",\"goal5_validation_accuracy\":"<<impl_->goal5.evaluate(impl_->goal5data.val).answer_accuracy
      <<",\"goal6_step\":"<<impl_->goal6.step<<",\"goal6_validation_accuracy\":"<<impl_->goal6.evaluate(impl_->goal6data.val).answer_accuracy
      <<",\"pretrained\":false}";
    return s.str();
}

} // namespace motorai
