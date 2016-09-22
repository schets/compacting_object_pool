#include "tree.hpp"
#include "pool.hpp"

__attribute__ ((noinline)) tree *alloc_tree(base_compacting_pool<16, 8> &pool) {
    return (tree *)pool.alloc();
}

tree *build_tree(base_compacting_pool<16, 8> &pool, int depth, int maxdepth) {
    if (depth >= maxdepth) return nullptr;
    tree * volatile to_make = (tree *)pool.alloc();
    to_make->right = build_tree(pool, depth+1, maxdepth);
    to_make->left = build_tree(pool, depth+1, maxdepth);
    return (tree *)to_make;
}

void free_tree(base_compacting_pool<16, 8> &pool, tree *&root, int depth) {
    if (root == nullptr) return;
    free_tree(pool, root->right, depth + 1);
    free_tree(pool, root->left, depth + 1);
    pool.free(root);
    root = nullptr;
}

int main() {
    base_compacting_pool<16, 8> pool;
    tree *t = build_tree(pool, 0, 26);
    free_tree(pool, t, 0);
    pool.clean();
}
