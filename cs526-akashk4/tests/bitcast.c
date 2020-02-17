struct RT {
    int v;
    int u;
};
struct ST {
    int p;
    int q;
};
int main () {
    struct ST *st;
    struct RT *rt;
    *st = (*(struct ST*)rt);
    
    return 0;
}
