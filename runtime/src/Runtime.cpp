#include <stdio.h>

#include "Runtime.h"
#include <string>
#include <stdint.h>
#include <iostream>
#include <signal.h>
#include <unistd.h>

// AVL tree implementation in C++

#define max(a, b) ((a > b) ? a : b)

#define DEBUG_PRINT_ENABLE

#ifdef DEBUG_PRINT_ENABLE
#define DBG(x) x
#else
#define DBG(x)
#endif

using namespace std;

class Node
{
public:
    uint64_t key;
    uint64_t size;
    Node *left;
    Node *right;
    int height;
};

// Thanks to Micheal Sambol on youtube & github for their AVL tree implementation
// https://github.com/msambol/dsa/blob/master/trees/avl_tree.py (MIT license)

class AVLTree
{
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
    bool _CheckPoison(Node* root, uint64_t probe, uint8_t readWidth, Node* leftPar, Node* rightPar);
    void _reset(Node* root);
    void _printTree(Node *root, std::string indent, bool last);
public:
    void InsertRedzone(uint64_t start, uint64_t size);
    void RemoveRedzone(uint64_t start);
    bool CheckPoison(uint64_t probe, uint8_t readWidth);
    void reset();
    void printTree();
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
int AVLTree::height(Node *N)
{
    if (N == NULL)
        return 0;
    return N->height;
}

// Rotate right
Node *AVLTree::rightRotate(Node *y)
{
    Node *x = y->left;
    Node *T2 = x->right;
    x->right = y;
    y->left = T2;
    y->height = max(height(y->left), height(y->right)) + 1;
    x->height = max(height(x->left), height(x->right)) + 1;
    return x;
}

// Rotate left
Node *AVLTree::leftRotate(Node *x)
{
    Node *y = x->right;
    Node *T2 = y->left;
    y->left = x;
    x->right = T2;
    x->height = max(height(x->left), height(x->right)) +1;
    y->height = max(height(y->left), height(y->right)) + 1;
    return y;
}

// Get the balance factor of each node
int AVLTree::getBalanceFactor(Node *N)
{
    if (N == NULL)
        return 0;
    return height(N->left) - height(N->right);
}

// Insert a node
Node *AVLTree::insertNode(Node *node, uint64_t key, uint64_t size)
{
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
    node->height = 1 + max(height(node->left),
                           height(node->right));
    int balanceFactor = getBalanceFactor(node);
    if (balanceFactor > 1)
    {
        if (key < node->left->key)
        {
            return rightRotate(node);
        }
        else if (key > node->left->key)
        {
            node->left = leftRotate(node->left);
            return rightRotate(node);
        }
    }
    if (balanceFactor < -1)
    {
        if (key > node->right->key)
        {
            return leftRotate(node);
        }
        else if (key < node->right->key)
        {
            node->right = rightRotate(node->right);
            return leftRotate(node);
        }
    }
    return node;
}

// Node with minimum value
Node *AVLTree::nodeWithMimumValue(Node *node)
{
    Node *current = node;
    while (current->left != NULL)
        current = current->left;
    return current;
}

// Delete a node
Node *AVLTree::deleteNode(Node *root, uint64_t key)
{
    // Find the node and delete it
    if (root == NULL)
        return root;
    if (key < root->key)
        root->left = deleteNode(root->left, key);
    else if (key > root->key)
        root->right = deleteNode(root->right, key);
    else
    {
        if ((root->left == NULL) ||
            (root->right == NULL))
        {
            Node *temp = root->left ? root->left : root->right;
            if (temp == NULL)
            {
                temp = root;
                root = NULL;
            }
            else
                *root = *temp;
            delete (temp);
        }
        else
        {
            Node *temp = nodeWithMimumValue(root->right);
            root->key = temp->key;
            root->right = deleteNode(root->right,
                                     temp->key);
        }
    }

    if (root == NULL)
        return root;

    // Update the balance factor of each node and
    // balance the tree
    root->height = 1 + max(height(root->left),
                           height(root->right));
    int balanceFactor = getBalanceFactor(root);
    if (balanceFactor > 1)
    {
        if (getBalanceFactor(root->left) >= 0)
        {
            return rightRotate(root);
        }
        else
        {
            root->left = leftRotate(root->left);
            return rightRotate(root);
        }
    }
    if (balanceFactor < -1)
    {
        if (getBalanceFactor(root->right) <= 0)
        {
            return leftRotate(root);
        }
        else
        {
            root->right = rightRotate(root->right);
            return leftRotate(root);
        }
    }
    return root;
}

// Print the tree
void AVLTree::_printTree(Node *root, string indent, bool last)
{
    if (root != nullptr)
    {
        std::cout << indent;
        if (last)
        {
            std::cout << "R----";
            indent += "   ";
        }
        else
        {
            std::cout << "L----";
            indent += "|  ";
        }
        std::cout << std::hex << root->key << std::endl;
        _printTree(root->left, indent, false);
        _printTree(root->right, indent, true);
    }
}


/**
 * If you go left, you're the right parent and vice versa
 * When done, one is exactly between the left and right node.
 */
bool AVLTree::_CheckPoison(Node* root, uint64_t probe, uint8_t readWidth, Node* leftPar, Node* rightPar){
    
    DBG(cout << std::hex << "probe: " << probe << " on node " << (root? root->key : 0);)
    

    if (root == NULL){
        // cout << " is null\n";
        // return false;
    }
    else if (root->key == probe){
        DBG(cout << " exact hit";)
        leftPar = root;
    }
    else if (root->key < probe){
        DBG(cout << " Right\n";)
        return _CheckPoison(root->right, probe, readWidth, root, rightPar);
    }
    else if (root->key > probe){
        DBG(cout << " Left\n";)
        return _CheckPoison(root->left, probe, readWidth, leftPar, root);
    }

    uint64_t leftKey = leftPar != NULL? leftPar->key : 0;
    uint64_t rightKey = rightPar != NULL? rightPar->key : 0;
    DBG(cout << " left: " << leftKey << " right: " << rightKey;)
    
    /**
     * Left parent is the node which is immediately left in the ordering. If null,
     * there is no node immediately left of us in the ordering, which happens if 
     * we are the left-most node. Right is similar.
     */
    if (leftPar != NULL && (leftPar->key + leftPar->size) > probe)
    {
        DBG(cout << " first byte hit\n";)
        return true;
    }
    else if (rightPar != NULL && (rightPar->key) < (probe + readWidth))
    {
        DBG(cout << " partial overflow detected!\n";)
        return true;
    }
    DBG(cout << " all clear\n";)
    return false;
}

void AVLTree::_reset(Node* root){
    if (root == nullptr)
    {
        return;
    }
    _reset(root->right);
    _reset(root->left);
    delete root;
}

#pragma endregion

#pragma region

void AVLTree::InsertRedzone(uint64_t start, uint64_t size)
{   
    root = insertNode(root, start, size);
}

void AVLTree::RemoveRedzone(uint64_t start)
{
    root = deleteNode(root, start);
}

bool AVLTree::CheckPoison(uint64_t probe, uint8_t readWidth)
{
    return _CheckPoison(root, probe, readWidth, NULL, NULL);
}
void AVLTree::reset(){
     _reset(root);
     root = nullptr;
}

void AVLTree::printTree(){
    _printTree(root, "", false);
}
#pragma endregion

AVLTree redzones;

void __rdzone_check(void* probe, uint8_t op_width){
    if(redzones.CheckPoison((uint64_t)probe, op_width)){
        cerr << "ILLEGAL ACCESS AT " << probe << "\n";
        kill(getpid(), SIGABRT);
    }
}

void __rdzone_add(void* start, uint64_t size){
    redzones.InsertRedzone((uint64_t)start, size);
}
void __rdzone_rm(void* start){
    redzones.RemoveRedzone((uint64_t)start);
}

void __rdzone_reset(){
    redzones.reset();
}

void __rdzone_dbg_print(){
    redzones.printTree();
}

// You can write anything here and it will be invisible to the outside as it
// has internal linkage.
void test_runtime_link()
{
    printf("runtime initalised!\n");
}
