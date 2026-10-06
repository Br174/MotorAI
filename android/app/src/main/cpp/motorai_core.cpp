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

struct Param {
    std::string name;
    Tensor value;
    std::vector<float> m,v;
};

class TinyTransformer {
public:
    int vocab=11, context=7, d=32, ff=64;
    std::vector<Param> p;
    std::mt19937 rng;

    explicit TinyTransformer(uint32_t seed):rng(seed){ init(); }

    Tensor& P(const std::string& name){ for(auto& z:p)if(z.name==name)return z.value; throw std::runtime_error("missing param "+name); }
    const Tensor& P(const std::string& name) const { for(auto& z:p)if(z.name==name)return z.value; throw std::runtime_error("missing param "+name); }

    void addParam(const std::string& name,std::vector<int> shape,bool normal=true,float fill=0.0f){
        std::vector<float> data(numel(shape));
        if(normal){ for(float&x:data)x=0.02f*deterministicNormalApprox(rng);} else std::fill(data.begin(),data.end(),fill);
        Tensor t=tensor(shape,std::move(data),true); p.push_back({name,t,std::vector<float>(t.size(),0),std::vector<float>(t.size(),0)});
    }
    void init(){
        p.clear(); addParam("token",{vocab,d}); addParam("pos",{context,d});
        addParam("ln1.g",{d},false,1); addParam("ln1.b",{d},false,0);
        addParam("q.w",{d,d}); addParam("q.b",{d},false,0); addParam("k.w",{d,d}); addParam("k.b",{d},false,0); addParam("v.w",{d,d}); addParam("v.b",{d},false,0); addParam("o.w",{d,d}); addParam("o.b",{d},false,0);
        addParam("ln2.g",{d},false,1); addParam("ln2.b",{d},false,0);
        addParam("fc1.w",{d,ff}); addParam("fc1.b",{ff},false,0); addParam("fc2.w",{ff,d}); addParam("fc2.b",{d},false,0);
        addParam("lnf.g",{d},false,1); addParam("lnf.b",{d},false,0); addParam("head.w",{d,vocab});
    }
    int parameterCount() const { int n=0; for(auto&z:p)n+=z.value.size(); return n; }
    void zeroGrad(){ for(auto&z:p) std::fill(z.value.n->grad.begin(),z.value.n->grad.end(),0.0f); }

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
        Tensor zf=layerNorm(x2,P("lnf.g"),P("lnf.b"));
        return matmul(zf,P("head.w"));
    }
    Tensor loss(const std::vector<int>& x,const std::vector<int>& y){ return crossEntropy(forward(x),y); }

    void adamStep(float lr,int batch,int step){
        double sq=0; for(auto&z:p)for(float g:z.value.n->grad){ float gg=g/batch; sq+=double(gg)*gg; }
        float norm=std::sqrt((float)sq), clip=norm>1.0f?1.0f/(norm+1e-8f):1.0f;
        const float b1=.9f,b2=.999f,eps=1e-8f; float bc1=1-std::pow(b1,(float)step),bc2=1-std::pow(b2,(float)step);
        for(auto&z:p) for(size_t i=0;i<z.value.n->data.size();++i){ float g=z.value.n->grad[i]/batch*clip; z.m[i]=b1*z.m[i]+(1-b1)*g; z.v[i]=b2*z.v[i]+(1-b2)*g*g; float mh=z.m[i]/bc1,vh=z.v[i]/bc2; z.value.n->data[i]-=lr*mh/(std::sqrt(vh)+eps); }
    }
};

struct Example {
    std::vector<int> x,y;
    std::string raw;
    int answer_start = 0;
    int answer_len = 0;
};

struct Dataset {
    std::vector<Example> train,val,test,retention;

    static int id(char c){ if(c=='\n')return 0; if(c=='>')return 1; if(c>='a'&&c<='i')return 2+(c-'a'); return -1; }
    static char ch(int id){ if(id==0)return '\n'; if(id==1)return '>'; if(id>=2&&id<=10)return char('a'+id-2); return '?'; }

    static Example encode(const std::string&s){
        std::vector<int>a;
        for(char c:s){ int k=id(c); if(k<0)throw std::runtime_error("unsupported char"); a.push_back(k); }
        auto sep=s.find('>');
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

    static std::string level1Raw(char task,const std::string&pair){
        std::string out=pair;
        if(task=='b') std::reverse(out.begin(),out.end());
        else if(task=='c' && out[0]>out[1]) std::swap(out[0],out[1]);
        std::string raw; raw+=task; raw+=pair; raw+='>'; raw+=out; raw+='\n';
        return raw;
    }

    explicit Dataset(uint32_t seed,int level=0){
        std::vector<Example> baseTrain,baseVal,baseTest;
        buildCopy3(seed,baseTrain,baseVal,baseTest);
        if(level<=0){
            train=std::move(baseTrain); val=std::move(baseVal); test=std::move(baseTest); retention=test;
            return;
        }

        retention=baseTest;
        std::vector<std::string> pairs; std::string symbols="defghi";
        for(char a:symbols)for(char b:symbols)if(a!=b){ std::string p; p+=a; p+=b; pairs.push_back(p); }
        std::mt19937 r(seed+1009); deterministicShuffle(pairs,r);

        const char tasks[3]={'a','b','c'};
        for(size_t i=0;i<pairs.size();++i){
            auto& target = (i<18) ? train : ((i<24) ? val : test);
            for(char task:tasks) target.push_back(encode(level1Raw(task,pairs[i])));
        }

        // Replay controllato del Livello 0 per ridurre il catastrophic forgetting.
        for(size_t i=0;i<30 && i<baseTrain.size();++i) train.push_back(baseTrain[i]);
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
    uint32_t seed=174; TinyTransformer model; int curriculum=0; Dataset data; int step=0; std::atomic<bool> pause{false}; mutable std::mutex mu;
    explicit Impl(uint32_t s):seed(s),model(s),curriculum(0),data(s,0){}
};

Engine::Engine(uint32_t seed):impl_(std::make_unique<Impl>(seed)){}
Engine::~Engine()=default;
void Engine::reset(uint32_t seed){ impl_=std::make_unique<Impl>(seed); }
Metrics Engine::evaluateTrain(){ std::lock_guard<std::mutex> g(impl_->mu); return eval(impl_->model,impl_->data.train); }
Metrics Engine::evaluateValidation(){ std::lock_guard<std::mutex> g(impl_->mu); return eval(impl_->model,impl_->data.val); }
Metrics Engine::evaluateTest(){ std::lock_guard<std::mutex> g(impl_->mu); return eval(impl_->model,impl_->data.test); }
Metrics Engine::evaluateRetention(){ std::lock_guard<std::mutex> g(impl_->mu); return eval(impl_->model,impl_->data.retention); }
int Engine::parameterCount() const{return impl_->model.parameterCount();}
int Engine::globalStep() const{return impl_->step;}
void Engine::setCurriculum(int level){ std::lock_guard<std::mutex> g(impl_->mu); level=level<=0?0:1; if(impl_->curriculum==level)return; impl_->curriculum=level; impl_->data=Dataset(impl_->seed,level); }
int Engine::curriculumLevel() const{ std::lock_guard<std::mutex> g(impl_->mu); return impl_->curriculum; }
void Engine::requestPause(){impl_->pause.store(true);} void Engine::clearPause(){impl_->pause.store(false);}

TrainResult Engine::train(int steps,int batch,float lr){
    std::lock_guard<std::mutex> guard(impl_->mu);
    clearPause(); auto t0=std::chrono::steady_clock::now(); std::mt19937 r(impl_->seed+1+impl_->step); int done=0;
    for(int s=0;s<steps;++s){ if(impl_->pause.load())break; impl_->model.zeroGrad(); for(int b=0;b<batch;++b){auto&e=impl_->data.train[deterministicIndex(r, impl_->data.train.size())]; Tensor L=impl_->model.loss(e.x,e.y); backward(L);} ++impl_->step; impl_->model.adamStep(lr,batch,impl_->step); ++done; }
    auto t1=std::chrono::steady_clock::now(); TrainResult tr; tr.steps_completed=done; tr.train=eval(impl_->model,impl_->data.train); tr.validation=eval(impl_->model,impl_->data.val); tr.test=eval(impl_->model,impl_->data.test); tr.retention=eval(impl_->model,impl_->data.retention); tr.elapsed_seconds=std::chrono::duration<double>(t1-t0).count(); tr.paused=impl_->pause.load(); return tr;
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
        const char magic[8]={'M','O','T','A','I','0','0','5'}; w.write(magic,8);
        uint32_t ver=2,seed=impl_->seed,step=impl_->step,level=impl_->curriculum,pc=impl_->model.p.size();
        w.write((char*)&ver,4); w.write((char*)&seed,4); w.write((char*)&step,4); w.write((char*)&level,4); w.write((char*)&pc,4);
        for(auto&z:impl_->model.p){
            uint32_t nl=z.name.size(),sz=z.value.n->data.size();
            w.write((char*)&nl,4); w.write(z.name.data(),nl); w.write((char*)&sz,4);
            w.write((char*)z.value.n->data.data(),sz*sizeof(float));
            w.write((char*)z.m.data(),sz*sizeof(float)); w.write((char*)z.v.data(),sz*sizeof(float));
        }
        w.close();
        Metrics te=eval(const_cast<TinyTransformer&>(impl_->model),impl_->data.test);
        Metrics re=eval(const_cast<TinyTransformer&>(impl_->model),impl_->data.retention);
        std::ofstream j(dir+"/checkpoint.json");
        j<<"{\n  \"format\": \"MOTORAI_CHECKPOINT_NATIVE_V2\",\n  \"seed\": "<<impl_->seed
         <<",\n  \"global_step\": "<<impl_->step<<",\n  \"curriculum_level\": "<<impl_->curriculum
         <<",\n  \"parameter_count\": "<<impl_->model.parameterCount()
         <<",\n  \"test_loss\": "<<te.loss<<",\n  \"test_answer_accuracy\": "<<te.answer_accuracy
         <<",\n  \"retention_accuracy\": "<<re.answer_accuracy
         <<",\n  \"pretrained_model\": false,\n  \"weights_origin\": \"random_then_local_training\"\n}\n";
        return (bool)j;
    }catch(...){return false;}
}

bool Engine::loadCheckpoint(const std::string&dir){
    std::lock_guard<std::mutex> guard(impl_->mu);
    try{
        std::ifstream w(dir+"/weights.bin",std::ios::binary); if(!w)return false;
        char magic[8]; w.read(magic,8); std::string m(magic,8);
        uint32_t ver=0,seed=0,step=0,level=0,pc=0;
        if(m=="MOTAI004"){
            w.read((char*)&ver,4); w.read((char*)&seed,4); w.read((char*)&step,4); w.read((char*)&pc,4);
            if(ver!=1) return false;
            level=0;
        }else if(m=="MOTAI005"){
            w.read((char*)&ver,4); w.read((char*)&seed,4); w.read((char*)&step,4); w.read((char*)&level,4); w.read((char*)&pc,4);
            if(ver!=2 || level>1) return false;
        }else return false;
        if(pc!=impl_->model.p.size())return false;
        for(auto&z:impl_->model.p){
            uint32_t nl,sz; w.read((char*)&nl,4); std::string name(nl,' '); w.read(name.data(),nl); w.read((char*)&sz,4);
            if(name!=z.name||sz!=z.value.n->data.size())return false;
            w.read((char*)z.value.n->data.data(),sz*sizeof(float));
            w.read((char*)z.m.data(),sz*sizeof(float)); w.read((char*)z.v.data(),sz*sizeof(float));
        }
        if(!w)return false;
        impl_->seed=seed; impl_->curriculum=static_cast<int>(level); impl_->data=Dataset(seed,impl_->curriculum); impl_->step=step;
        return true;
    }catch(...){return false;}
}

std::string Engine::statusJson() const{
    std::lock_guard<std::mutex> guard(impl_->mu);
    Metrics te=eval(impl_->model,impl_->data.test), re=eval(impl_->model,impl_->data.retention);
    std::ostringstream s; s<<std::fixed<<std::setprecision(4)
      <<"{\"seed\":"<<impl_->seed<<",\"step\":"<<impl_->step<<",\"curriculum\":"<<impl_->curriculum
      <<",\"parameters\":"<<parameterCount()<<",\"test_loss\":"<<te.loss<<",\"test_accuracy\":"<<te.answer_accuracy
      <<",\"retention_accuracy\":"<<re.answer_accuracy<<",\"pretrained\":false}";
    return s.str();
} // namespace motorai
