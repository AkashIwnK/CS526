struct RT {
    int v;
    int u;
};
struct ST {
    float *p;
    float *q;
};
int main () {
    struct ST st;
    struct RT rt;
    st.p = (float *)&rt.v;
    st.q = (float *)&rt.u;
    return 0;
}
