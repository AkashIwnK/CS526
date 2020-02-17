struct RT {
    int v;
};
struct ST {
    struct RT *p;
};
int main () {
    struct ST st;
    struct RT rt;
    st.p = &rt;
    return 0;
}
