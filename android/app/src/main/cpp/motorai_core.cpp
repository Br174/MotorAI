#include "motorai_core.h"

#include <algorithm>
#include <chrono>
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
        addParam("grow3.cls.w",{d,2}); addParam("grow3.cls.b",{2},false,0);
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
            xa=add(x2,p3);
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

        Tensor zf=layerNorm(xg3,P("lnf.g"),P("lnf.b"));
        Tensor logits=matmul(zf,P("head.w"));

        if(l5control){
            Tensor cls=linear(zf,"grow3.cls.w","grow3.cls.b");
            for(int i=0;i<logits.dim(0);++i){
                logits.n->data[i*vocab+12]+=cls.n->data[i*2+0];
                logits.n->data[i*vocab+13]+=cls.n->data[i*2+1];
            }
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
        // Auto-Training V1: relazione tra i primi due simboli, terzo simbolo come distrattore.
        // Ogni coppia a-i compare in TRAIN/VAL/TEST ma con distrattori disgiunti.
        std::string symbols="abcdefghi";
        std::mt19937 r(seed);

        auto append=[&](char a,char b,char x,std::vector<Example>& dst){
            char label=(a==b)?'+':'-';
            std::string q; q+=a; q+=b; q+=x;
            std::string out(1,label);
            dst.push_back(encode("!"+q+">"+out+"\n"));
        };

        for(char a:symbols) for(char b:symbols){
            std::vector<char> distractors(symbols.begin(),symbols.end());
            // Rotazione deterministica diversa per coppia, poi shuffle con il seed del curriculum.
            std::rotate(distractors.begin(),
                        distractors.begin()+((a-'a')*3+(b-'a'))%distractors.size(),
                        distractors.end());
            deterministicShuffle(distractors,r);

            for(int k=0;k<9;++k){
                if(k<5) append(a,b,distractors[k],tr);
                else if(k<7) append(a,b,distractors[k],va);
                else append(a,b,distractors[k],te);
            }
        }

        // Bilancia il TRAIN: 45 positivi base contro 360 negativi.
        // Duplichiamo solo i positivi nel TRAIN; validation/test restano intatti e indipendenti.
        std::vector<Example> positives;
        for(const auto& e:tr) if(e.raw.find(">+")!=std::string::npos) positives.push_back(e);
        for(int repeat=0;repeat<7;++repeat)
            for(const auto& e:positives) tr.push_back(e);

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

        // Livello 5: Auto-Training V1. Uguaglianza adiacente con distrattori mai visti.
        // Segmenti: L5=720, L4=160, L3=48, L2=48, L1=48, L0=72.
        train=l5tr; val=l5va; test=l5te;
        for(size_t i=0;i<160 && i<l4tr.size();++i) train.push_back(l4tr[i]);
        for(size_t i=0;i<48 && i<l3tr.size();++i) train.push_back(l3tr[i]);
        for(size_t i=0;i<48 && i<l2tr.size();++i) train.push_back(l2tr[i]);
        for(size_t i=0;i<48 && i<l1tr.size();++i) train.push_back(l1tr[i]);
        for(size_t i=0;i<72 && i<l0tr.size();++i) train.push_back(l0tr[i]);
    }
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
    uint32_t seed=174; TinyTransformer model; int curriculum=0; int curriculum_start_step=0; Dataset data; int step=0; std::atomic<bool> pause{false}; mutable std::mutex mu;
    explicit Impl(uint32_t s):seed(s),model(s),curriculum(0),curriculum_start_step(0),data(s,0){}
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
void Engine::requestPause(){impl_->pause.store(true);} void Engine::clearPause(){impl_->pause.store(false);}

TrainResult Engine::train(int steps,int batch,float lr){
    std::lock_guard<std::mutex> guard(impl_->mu);
    clearPause();
    auto t0=std::chrono::steady_clock::now();
    std::mt19937 r(impl_->seed+1+impl_->step);
    int done=0;
    float effective_lr=(impl_->curriculum>=5)?lr*2.0f:((impl_->curriculum>=4)?lr:((impl_->curriculum>=3)?lr*0.65f:((impl_->curriculum>=2)?lr*0.75f:lr)));

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
            }else if(impl_->curriculum>=5 && impl_->data.train.size()>=1096){
                // L5 modular and isolated: 100% new-task batches.
                idx=deterministicIndex(r,720);
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
        const char magic[8]={'M','O','T','A','I','0','1','1'}; w.write(magic,8);
        uint32_t ver=5,seed=impl_->seed,step=impl_->step,level=impl_->curriculum,start_step=impl_->curriculum_start_step,pc=impl_->model.p.size();
        w.write((char*)&ver,4); w.write((char*)&seed,4); w.write((char*)&step,4); w.write((char*)&level,4); w.write((char*)&start_step,4); w.write((char*)&pc,4);
        for(auto&z:impl_->model.p){
            uint32_t nl=z.name.size(),sz=z.value.n->data.size();
            w.write((char*)&nl,4); w.write(z.name.data(),nl); w.write((char*)&sz,4);
            w.write((char*)z.value.n->data.data(),sz*sizeof(float));
            w.write((char*)z.m.data(),sz*sizeof(float)); w.write((char*)z.v.data(),sz*sizeof(float));
        }
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
        impl_->seed=seed; impl_->curriculum=static_cast<int>(level); impl_->curriculum_start_step=static_cast<int>(start_step); impl_->data=Dataset(seed,impl_->curriculum); impl_->step=step;
        return true;
    }catch(...){return false;}
}

std::string Engine::statusJson() const{
    std::lock_guard<std::mutex> guard(impl_->mu);
    Metrics te=eval(impl_->model,impl_->data.test), r0=eval(impl_->model,impl_->data.retention_l0), r1=eval(impl_->model,impl_->data.retention_l1), r2=eval(impl_->model,impl_->data.retention_l2), r3=eval(impl_->model,impl_->data.retention_l3), r4=eval(impl_->model,impl_->data.retention_l4);
    std::ostringstream s; s<<std::fixed<<std::setprecision(4)
      <<"{\"seed\":"<<impl_->seed<<",\"step\":"<<impl_->step<<",\"curriculum\":"<<impl_->curriculum<<",\"curriculum_start_step\":"<<impl_->curriculum_start_step
      <<",\"parameters\":"<<parameterCount()<<",\"test_loss\":"<<te.loss<<",\"test_accuracy\":"<<te.answer_accuracy
      <<",\"retention_l0_accuracy\":"<<r0.answer_accuracy<<",\"retention_l1_accuracy\":"<<r1.answer_accuracy<<",\"retention_l2_accuracy\":"<<r2.answer_accuracy<<",\"retention_l3_accuracy\":"<<r3.answer_accuracy<<",\"retention_l4_accuracy\":"<<r4.answer_accuracy<<",\"pretrained\":false}";
    return s.str();
}

} // namespace motorai
