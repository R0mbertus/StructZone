#include <stdio.h>

#include "Runtime.h"
#include <assert.h>
#include <iostream>
#include <malloc.h>
#include <signal.h>
#include <stdint.h>
#include <string>
#include <unistd.h>
#include <vector>

// AVL tree implementation in C++

#define max(a, b) ((a > b) ? a : b)

#define DEBUG_PRINT_ENABLE

#ifdef DEBUG_PRINT_ENABLE
#define DBG(x) x
#else
#define DBG(x)
#endif

using namespace std;

class Node {
  public:
    uint64_t key;
    uint64_t size;
    Node *left;
    Node *right;
    int height;
};

// Thanks to Micheal Sambol on youtube & github for their AVL tree implementation
// https://github.com/msambol/dsa/blob/master/trees/avl_tree.py (MIT license)

class AVLTree {
  private:
    Node *root;

    int height(Node *N);
    Node *newNode(uint64_t key, uint64_t size);
    Node *rightRotate(Node *y);
    Node *leftRotate(Node *x);
    int getBalanceFactor(Node *N);
    Node *insertNode(Node *node, uint64_t key, uint64_t size);
    Node *nodeWithMimumValue(Node *node);
    Node *deleteNode(Node *root, uint64_t key);
    bool _CheckPoison(Node *root, uint64_t probe, uint8_t readWidth, Node *leftPar, Node *rightPar);
    void _reset(Node *root);
    void _printTree(Node *root, std::string indent, bool last);
    void _get_between(Node *root, uint64_t start, uint64_t end, std::vector<Node *> *to_Rm);

  public:
    void InsertRedzone(uint64_t start, uint64_t size);
    void RemoveRedzone(uint64_t start);
    bool CheckPoison(uint64_t probe, uint8_t readWidth);
    void reset();
    void printTree();
    void remove_between(uint64_t start, uint64_t end);
};

#pragma region AVLtree

// New node creation
Node *AVLTree::newNode(uint64_t key, uint64_t size) {
    Node *node = new Node();
    node->key = key;
    node->size = size;
    node->left = NULL;
    node->right = NULL;
    node->height = 1;
    return (node);
}

// Calculate height
int AVLTree::height(Node *N) {
    if (N == NULL)
        return 0;
    return N->height;
}

// Rotate right
Node *AVLTree::rightRotate(Node *y) {
    Node *x = y->left;
    Node *T2 = x->right;
    x->right = y;
    y->left = T2;
    y->height = max(height(y->left), height(y->right)) + 1;
    x->height = max(height(x->left), height(x->right)) + 1;
    return x;
}

// Rotate left
Node *AVLTree::leftRotate(Node *x) {
    Node *y = x->right;
    Node *T2 = y->left;
    y->left = x;
    x->right = T2;
    x->height = max(height(x->left), height(x->right)) + 1;
    y->height = max(height(y->left), height(y->right)) + 1;
    return y;
}

// Get the balance factor of each node
int AVLTree::getBalanceFactor(Node *N) {
    if (N == NULL)
        return 0;
    return height(N->left) - height(N->right);
}

// Insert a node
Node *AVLTree::insertNode(Node *node, uint64_t key, uint64_t size) {
    // Find the correct postion and insert the node
    if (node == NULL)
        return (newNode(key, size));
    if (key < node->key)
        node->left = insertNode(node->left, key, size);
    else if (key > node->key)
        node->right = insertNode(node->right, key, size);
    else
        return node;

    // Update the balance factor of each node and
    // balance the tree
    node->height = 1 + max(height(node->left), height(node->right));
    int balanceFactor = getBalanceFactor(node);
    if (balanceFactor > 1) {
        if (key < node->left->key) {
            return rightRotate(node);
        } else if (key > node->left->key) {
            node->left = leftRotate(node->left);
            return rightRotate(node);
        }
    }
    if (balanceFactor < -1) {
        if (key > node->right->key) {
            return leftRotate(node);
        } else if (key < node->right->key) {
            node->right = rightRotate(node->right);
            return leftRotate(node);
        }
    }
    return node;
}

// Node with minimum value
Node *AVLTree::nodeWithMimumValue(Node *node) {
    Node *current = node;
    while (current->left != NULL)
        current = current->left;
    return current;
}

// Delete a node
Node *AVLTree::deleteNode(Node *root, uint64_t key) {
    // Find the node and delete it
    if (root == NULL)
        return root;
    if (key < root->key)
        root->left = deleteNode(root->left, key);
    else if (key > root->key)
        root->right = deleteNode(root->right, key);
    else {
        if ((root->left == NULL) || (root->right == NULL)) {
            Node *temp = root->left ? root->left : root->right;
            if (temp == NULL) {
                temp = root;
                root = NULL;
            } else
                *root = *temp;
            delete (temp);
        } else {
            Node *temp = nodeWithMimumValue(root->right);
            root->key = temp->key;
            root->right = deleteNode(root->right, temp->key);
        }
    }

    if (root == NULL)
        return root;

    // Update the balance factor of each node and
    // balance the tree
    root->height = 1 + max(height(root->left), height(root->right));
    int balanceFactor = getBalanceFactor(root);
    if (balanceFactor > 1) {
        if (getBalanceFactor(root->left) >= 0) {
            return rightRotate(root);
        } else {
            root->left = leftRotate(root->left);
            return rightRotate(root);
        }
    }
    if (balanceFactor < -1) {
        if (getBalanceFactor(root->right) <= 0) {
            return leftRotate(root);
        } else {
            root->right = rightRotate(root->right);
            return leftRotate(root);
        }
    }
    return root;
}

// Print the tree
void AVLTree::_printTree(Node *root, string indent, bool last) {
    if (root != nullptr) {
        cerr << indent;
        if (last) {
            cerr << "R----";
            indent += "   ";
        } else {
            cerr << "L----";
            indent += "|  ";
        }
        cerr << std::hex << root->key << std::endl;
        _printTree(root->left, indent, false);
        _printTree(root->right, indent, true);
    }
}

/**
 * If you go left, you're the right parent and vice versa
 * When done, one is exactly between the left and right node.
 */
bool AVLTree::_CheckPoison(Node *root, uint64_t probe, uint8_t readWidth, Node *leftPar,
                           Node *rightPar) {

    DBG(cerr << std::hex << "probe: " << probe << " on node " << (root ? root->key : 0);)

    if (root == NULL) {
        // cerr << " is null\n";
        // return false;
    } else if (root->key == probe) {
        DBG(cerr << " exact hit";)
        leftPar = root;
    } else if (root->key < probe) {
        DBG(cerr << " Right\n";)
        return _CheckPoison(root->right, probe, readWidth, root, rightPar);
    } else if (root->key > probe) {
        DBG(cerr << " Left\n";)
        return _CheckPoison(root->left, probe, readWidth, leftPar, root);
    }

    uint64_t leftKey = leftPar != NULL ? leftPar->key : 0;
    uint64_t rightKey = rightPar != NULL ? rightPar->key : 0;
    DBG(cerr << " left: " << leftKey << " right: " << rightKey;)

    /**
     * Left parent is the node which is immediately left in the ordering. If null,
     * there is no node immediately left of us in the ordering, which happens if
     * we are the left-most node. Right is similar.
     */
    if (leftPar != NULL && (leftPar->key + leftPar->size) > probe) {
        DBG(cerr << " first byte hit\n";)
        return true;
    } else if (rightPar != NULL && (rightPar->key) < (probe + readWidth)) {
        DBG(cerr << " partial overflow detected!\n";)
        return true;
    }
    DBG(cerr << " all clear\n";)
    return false;
}

void AVLTree::_reset(Node *root) {
    if (root == nullptr) {
        return;
    }
    _reset(root->right);
    _reset(root->left);
    delete root;
}

void AVLTree::_get_between(Node *root, uint64_t start, uint64_t end, std::vector<Node *> *to_Ret) {
    DBG(cerr << std::hex << "looking for: " << start << " to " << end << " on node "
             << (root ? root->key : 0);)
    assert(start < end);
    if (root == NULL) {
        DBG(cerr << "\n");
        return;
    } else if (root->key >= start && root->key <= end) {
        DBG(cerr << " M\n");
        to_Ret->push_back(root);
        _get_between(root->left, start, end, to_Ret);
        _get_between(root->right, start, end, to_Ret);

    } else if (root->key > end) {
        DBG(cerr << " L\n");
        _get_between(root->left, start, end, to_Ret);
    } else if (root->key < start) {
        DBG(cerr << " R\n");
        _get_between(root->right, start, end, to_Ret);
    }
}

#pragma endregion

#pragma region

void AVLTree::InsertRedzone(uint64_t start, uint64_t size) { root = insertNode(root, start, size); }

void AVLTree::RemoveRedzone(uint64_t start) { root = deleteNode(root, start); }

bool AVLTree::CheckPoison(uint64_t probe, uint8_t readWidth) {
    return _CheckPoison(root, probe, readWidth, NULL, NULL);
}
void AVLTree::reset() {
    _reset(root);
    root = nullptr;
}
void AVLTree::printTree() { _printTree(root, "", false); }
void AVLTree::remove_between(uint64_t start, uint64_t end) {
    assert(start < end);
    std::vector<Node *> nodesToRm;
    _get_between(root, start, end, &nodesToRm);

    DBG(cerr << "Removing " << nodesToRm.size() << " nodes\n");
    for (Node *node : nodesToRm) {
        DBG(cerr << "rm " << node->key << "\n");
        RemoveRedzone(node->key);
    }
}
#pragma endregion

AVLTree redzones;

void __rdzone_check(void *probe, uint8_t op_width) {
    if (redzones.CheckPoison((uint64_t)probe, op_width)) {
        cerr << "ILLEGAL ACCESS AT " << probe << "\n";
        redzones.printTree();
        kill(getpid(), SIGABRT);
    }
}

void __rdzone_add(void *start, uint64_t size) { redzones.InsertRedzone((uint64_t)start, size); }
void __rdzone_rm(void *start) { redzones.RemoveRedzone((uint64_t)start); }

void __rdzone_reset() { redzones.reset(); }

void __rdzone_dbg_print() { redzones.printTree(); }

void __rdzone_heaprm(void *freed_ptr) {
    size_t size = malloc_usable_size(freed_ptr);
    redzones.remove_between((uint64_t)freed_ptr, (uint64_t)((char *)freed_ptr + size));
}

void __rdzone_rm_between(void *freed_ptr, size_t size) {
    redzones.remove_between((uint64_t)freed_ptr, (uint64_t)((char *)freed_ptr + size));
}

// You can write anything here and it will be invisible to the outside as it
// has internal linkage.
void test_runtime_link() { cerr << "runtime initislised!\n"; }
