#include "tree.hpp"
#include "pool.hpp"
#include <stdlib.h>
#include <utility>
#include <algorithm>
#include <iostream>

constexpr static size_t size_middle = 16000000 / sizeof(tree);
int32_t num_elems = 0;
int randn[2049];
size_t at = 0;
int32_t num_go = 65000;

using namespace std;
__attribute__ ((noinline)) tree *alloc_tree(base_compacting_pool<16, 8> &pool) {
    return (tree *)pool.alloc();
}

tree *build_tree(base_compacting_pool<16, 8> &pool, int depth, int maxdepth) {
    if (depth >= maxdepth) return nullptr;
    ++num_elems;
    tree * volatile to_make = (tree *)pool.alloc();
    to_make->right = build_tree(pool, depth+1, maxdepth);
    to_make->left = build_tree(pool, depth+1, maxdepth);
    return (tree *)to_make;
}

void free_tree(base_compacting_pool<16, 8> &pool, tree *&root) {
    if (root == nullptr) return;
    --num_elems;
    free_tree(pool, root->right);
    free_tree(pool, root->left);
    pool.free(root);
    root = nullptr;
}


static inline char getnext() {
    return randn[at++ & 2047];
}

void iter_down(base_compacting_pool<16, 8> &pool,tree *&root, int32_t value, int delete_at, int depth, bool addit) {
    int which_one = value & 1;
    if (root == nullptr) {
        if (addit) {
            root = build_tree(pool, 0, 4);
            return;
        }
    }
    else {
        tree *&whichdir = which_one ? root->left : root->right;
        if (depth == delete_at) {
            free_tree(pool, whichdir);
        }
        else {
            iter_down(pool, whichdir, value >> 1, delete_at, depth+1, addit);
        }
    }
}

void modify_tree(base_compacting_pool<16, 8> &pool, tree *&root, int numdo) {
    for (size_t n = 0; n < 10; n++) {
        for (int i = 0; i < 2049; i++) {
            randn[i] = (rand() >> 16) + (rand() & 0xffff0000);
        }
        for (size_t q = 0; q < 1000; q++) {
            for (size_t i = 0; i < 2048; i++) {
                int chance_del = 100;
                int32_t diff = num_elems - num_go;
                auto rdiff = diff;
                if (diff > 0) {
                    diff = min(num_go/10, diff);
                    diff = 32 - (diff / ((num_go/10) / 32));
                }
                if (diff > 16) {
                    diff = ((randn[i+1] >> 8) & 3) + 8;
                }
                // cout << diff << ", " << rdiff << ", " << num_elems << endl;
                iter_down(pool, root, randn[i], diff, 0, randn[i] & 1 || diff < 0);
            }
        }
    }
}

int main() {
    srand(100);
    base_compacting_pool<16, 8> pool;
    tree *t = build_tree(pool, 0, 16);
    modify_tree(pool, t, 0);
    free_tree(pool, t);
    pool.clean();
}
