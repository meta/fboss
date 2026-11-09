// Copyright 2004-present Facebook. All Rights Reserved.

#ifndef RADIX_TREE_H
#define RADIX_TREE_H

#include <sys/socket.h>
#include <algorithm>
#include <cstring>
#include <exception>
#include <functional>
#include <memory>
#include <string>
#include <utility>
#include <vector>

#include <glog/logging.h>

#include <folly/Conv.h>
#include <folly/IPAddress.h>
#include <folly/IPAddressV4.h>
#include <folly/IPAddressV6.h>
#include <folly/Memory.h>
#include <optional>

namespace facebook::network {
/*
 * Node in RadixTree, holds IP, mask. Will hold  value for nodes
 * created as a result of user inserts. Other type of nodes are
 * ones created by the radix tree implementation, which will
 * hold no values. All non value nodes will have 2 children,
 * this invariant must be maintained at all times.
 */
template <typename IPADDRTYPE, typename T>
class RadixTreeNode {
 public:
  // Optional function parameter to call from destructor
  using NodeDeleteCallback =
      std::function<void(const RadixTreeNode<IPADDRTYPE, T>&)>;

  RadixTreeNode(
      const IPADDRTYPE& ipAddr,
      uint8_t mlen,
      NodeDeleteCallback deleteCallback)
      : ipAddress_(ipAddr), masklen_(mlen), deleteCallback_(deleteCallback) {}

  template <typename VALUE>
  RadixTreeNode(
      const IPADDRTYPE& ipAddr,
      uint8_t mlen,
      VALUE&& val,
      NodeDeleteCallback deleteCallback)
      : ipAddress_(ipAddr),
        masklen_(mlen),
        value_(std::forward<VALUE>(val)),
        deleteCallback_(deleteCallback) {}

  ~RadixTreeNode() {
    if (deleteCallback_) {
      deleteCallback_(*this);
    }
  }

  enum class TreeDirection { LEFT, RIGHT, PARENT, THIS_NODE };

  const IPADDRTYPE& ipAddress() const {
    return ipAddress_;
  }
  bool isNonValueNode() const {
    return !isValueNode();
  }
  bool isValueNode() const {
    return value_.has_value();
  }
  uint32_t masklen() const {
    return masklen_;
  }
  const RadixTreeNode* left() const {
    return left_.get();
  }
  RadixTreeNode* left() {
    return left_.get();
  }
  const RadixTreeNode* right() const {
    return right_.get();
  }
  RadixTreeNode* right() {
    return right_.get();
  }
  RadixTreeNode* parent() {
    return parent_;
  }
  const RadixTreeNode* parent() const {
    return parent_;
  }
  bool isLeaf() const {
    return left_ == nullptr && right_ == nullptr;
  }
  const T& value() const {
    return value_.value();
  }
  T& value() {
    return value_.value();
  }
  NodeDeleteCallback nodeDeleteCallback() const {
    return deleteCallback_;
  }
  std::string str(bool printValue = true) const {
    auto nodeStr = folly::to<std::string>(ipAddress_.str(), "/", masklen_);
    if (printValue) {
      nodeStr += isNonValueNode()
          ? "(*)"
          : folly::to<std::string>("(", this->value(), ")");
    }
    return nodeStr;
  }

  // Given a IP, mask pair determine where that might lie w.r.t. this node
  TreeDirection searchDirection(const IPADDRTYPE& toSearch, uint8_t masklen)
      const;

  TreeDirection searchDirection(
      const RadixTreeNode<IPADDRTYPE, T>* node) const {
    return searchDirection(node->ipAddress_, node->masklen_);
  }

  // Comparison with links (left, right, parent) ignored
  bool equalSansLinks(const RadixTreeNode& r) const {
    return ipAddress_ == r.ipAddress_ && masklen_ == r.masklen_ &&
        isValueNode() == r.isValueNode() &&
        (!isValueNode() || this->value() == r.value());
  }

  std::unique_ptr<RadixTreeNode> resetLeft(
      std::unique_ptr<RadixTreeNode> newLeft) {
    auto old = std::move(left_);
    left_ = std::move(newLeft);
    if (left_) {
      left_->setParent(this);
    }
    return old;
  }

  std::unique_ptr<RadixTreeNode> resetRight(
      std::unique_ptr<RadixTreeNode> newRight) {
    auto old = std::move(right_);
    right_ = std::move(newRight);
    if (right_) {
      right_->setParent(this);
    }
    return old;
  }

  void setParent(RadixTreeNode* newParent) {
    parent_ = newParent;
  }

  template <typename VALUE>
  void setValue(VALUE&& newValue) {
    value_ = std::forward<VALUE>(newValue);
  }

  void makeNonValueNode() {
    value_.reset();
  }

 protected:
  IPADDRTYPE ipAddress_;
  uint32_t masklen_{0}; // Number of bits to match.
  std::optional<T> value_;
  std::unique_ptr<RadixTreeNode> left_{nullptr};
  std::unique_ptr<RadixTreeNode> right_{nullptr};
  RadixTreeNode* parent_{nullptr};
  NodeDeleteCallback deleteCallback_;
};

/*
 * Forward Iterator to traverse a Radix tree
 * Traverses the tree in DFS/preorder fashion
 */
template <
    typename IPADDRTYPE,
    typename T,
    typename CURSORNODE,
    typename DESIREDITERTYPE>
class RadixTreeIteratorImpl
    : public std::iterator<std::forward_iterator_tag, CURSORNODE> {
 public:
  using ValueType = DESIREDITERTYPE;
  using TreeNode = CURSORNODE;

  // default constructor
  RadixTreeIteratorImpl() {}
  explicit RadixTreeIteratorImpl(
      CURSORNODE* root,
      bool includeNonValNodes = false)
      : cursor_(root), includeNonValueNodes_(includeNonValNodes) {
    if (cursor_ && (!includeNonValueNodes_ && cursor_->isNonValueNode())) {
      ++(*this);
    }
    normalize();
  }

  DESIREDITERTYPE& operator++() {
    checkDereference(); // check if we are already at end
    radixTreeItrIncrement();
    return static_cast<DESIREDITERTYPE&>(*this);
  }

  DESIREDITERTYPE operator++(int) {
    DESIREDITERTYPE tmp(static_cast<DESIREDITERTYPE&>(*this));
    ++(*this);
    return tmp;
  }

  // Returns iterator over subtree of current node.
  DESIREDITERTYPE subTreeIterator() const {
    DESIREDITERTYPE tmp(static_cast<const DESIREDITERTYPE&>(*this));
    if (cursor_ != nullptr) {
      tmp.subTreeEnd_ = cursor_->parent();
      tmp.normalize();
    }
    return tmp;
  }

  void radixTreeItrIncrement();

  void reset() {
    cursor_ = nullptr;
    subTreeEnd_ = nullptr;
    includeNonValueNodes_ = false;
  }

  bool operator==(const RadixTreeIteratorImpl& r) const {
    return cursor_ == r.cursor_ && subTreeEnd_ == r.subTreeEnd_ &&
        includeNonValueNodes_ == r.includeNonValueNodes_;
  }

  bool operator!=(const RadixTreeIteratorImpl& r) const {
    return cursor_ != r.cursor_;
  }

  const CURSORNODE& operator*() const {
    checkDereference();
    return *cursor_;
  }

  const CURSORNODE* operator->() const {
    checkDereference();
    return cursor_;
  }

  CURSORNODE& operator*() {
    checkDereference();
    return *cursor_;
  }

  CURSORNODE* operator->() {
    checkDereference();
    return cursor_;
  }

  bool atEnd() const {
    return cursor_ == nullptr;
  }

  bool includeNonValueNodes() const {
    return includeNonValueNodes_;
  }

  void checkValueNode() const {
    CHECK(cursor_->isValueNode());
  }

 protected:
  void normalize() {
    if (cursor_ == nullptr) {
      reset();
    }
  }
  void checkDereference() const {
    CHECK(!atEnd());
  }
  CURSORNODE* cursor_{nullptr};
  CURSORNODE* subTreeEnd_{nullptr};
  bool includeNonValueNodes_{false};
};

/*
 * Iterator over a radix tree
 */
template <typename IPADDRTYPE, typename T>
class RadixTreeIterator : public RadixTreeIteratorImpl<
                              IPADDRTYPE,
                              T,
                              RadixTreeNode<IPADDRTYPE, T>,
                              RadixTreeIterator<IPADDRTYPE, T>> {
 public:
  using IteratorImpl = RadixTreeIteratorImpl<
      IPADDRTYPE,
      T,
      RadixTreeNode<IPADDRTYPE, T>,
      RadixTreeIterator<IPADDRTYPE, T>>;
  using TreeNode = typename IteratorImpl::TreeNode;
  using IteratorImpl::checkValueNode;

 private:
  using IteratorImpl::checkDereference;
  using IteratorImpl::cursor_;

 public:
  // Inherit constructors
  using IteratorImpl::IteratorImpl;

  template <typename VALUE>
  void setValue(VALUE&& value) const {
    checkDereference();
    checkValueNode();
    cursor_->setValue(std::forward<VALUE>(value));
  }
};

/*
 * Const Iterator over a radix tree
 */
template <typename IPADDRTYPE, typename T>
class RadixTreeConstIterator : public RadixTreeIteratorImpl<
                                   IPADDRTYPE,
                                   const T,
                                   const RadixTreeNode<IPADDRTYPE, T>,
                                   RadixTreeConstIterator<IPADDRTYPE, T>> {
 public:
  using IteratorImpl = RadixTreeIteratorImpl<
      IPADDRTYPE,
      const T,
      const RadixTreeNode<IPADDRTYPE, T>,
      RadixTreeConstIterator<IPADDRTYPE, T>>;
  using NonConstIterator = RadixTreeIterator<IPADDRTYPE, T>;
  using TreeNode = typename IteratorImpl::TreeNode;

  // Inherit constructors
  using IteratorImpl::IteratorImpl;
  // default constructor
  RadixTreeConstIterator() {}
  explicit RadixTreeConstIterator(NonConstIterator itr)
      : RadixTreeConstIterator(itr.node(), itr.includeNonValueNodes()) {}
};

template <typename IPADDRTYPE, typename T>
struct RadixTreeTraits {
  using Iterator = RadixTreeIterator<IPADDRTYPE, T>;
  using ConstIterator = RadixTreeConstIterator<IPADDRTYPE, T>;
  using TreeNode = RadixTreeNode<IPADDRTYPE, T>;

  Iterator makeItr(TreeNode* node, bool includeNonValueNodes = false) const {
    return Iterator(node, includeNonValueNodes);
  }

  Iterator itrConstCast(ConstIterator citr) const {
    return Iterator(
        citr.atEnd() ? nullptr : const_cast<TreeNode*>(&(*citr)),
        citr.includeNonValueNodes());
  }

  ConstIterator makeCItr(
      const TreeNode* node,
      bool includeNonValueNodes = false) const {
    return ConstIterator(node, includeNonValueNodes);
  }
};

template <
    typename IPADDRTYPE,
    typename T,
    typename TreeTraits = RadixTreeTraits<IPADDRTYPE, T>>
class RadixTree {
 public:
  using TreeNode = RadixTreeNode<IPADDRTYPE, T>;
  using TreeDirection = typename TreeNode::TreeDirection;
  using NodeDeleteCallback = typename TreeNode::NodeDeleteCallback;
  using Iterator = typename TreeTraits::Iterator;
  using ConstIterator = typename TreeTraits::ConstIterator;
  using VecConstIterators = typename std::vector<ConstIterator>;

  explicit RadixTree(
      NodeDeleteCallback nodeDelCallback = NodeDeleteCallback(),
      const TreeTraits& treeTraits = TreeTraits())
      : nodeDeleteCallback_(nodeDelCallback), traits_(treeTraits) {}

  RadixTree(const RadixTree& r) = delete;
  RadixTree& operator=(const RadixTree& r) = delete;

  Iterator begin() {
    return traits_.makeItr(root_.get());
  }
  Iterator end() {
    return traits_.makeItr(nullptr);
  }
  ConstIterator begin() const {
    return traits_.makeCItr(root_.get());
  }
  ConstIterator end() const {
    return traits_.makeCItr(nullptr);
  }

  // Free all nodes and clear the tree.
  void clear() {
    root_.reset(nullptr);
    size_ = 0;
  }
  RadixTree(RadixTree&& r) noexcept
      : nodeDeleteCallback_(r.nodeDeleteCallback_), traits_(r.traits_) {
    *this = std::move(r);
  }
  // Move radix tree onto this
  RadixTree& operator=(RadixTree&& r) noexcept {
    // Don't copy the traits and delete callback, use
    // ones with which this Radix tree was created
    size_ = r.size_;
    makeRoot(std::move(r.root_));
    r.size_ = 0;
    return *this;
  }
  // Clone this radix tree onto another
  template <typename U = T>
  typename std::enable_if<std::is_copy_constructible<U>::value, RadixTree>::type
  clone() const {
    static_assert(
        std::is_same<T, U>::value,
        "clone template type must be the same as Radix tree value type");
    RadixTree copy(nodeDeleteCallback_, traits_);
    copy.size_ = size_;
    copy.root_ = cloneSubTree(root_.get());
    return copy;
  }
  /*
   * Insert a IP, mask, value in tree. Returns inserted node, true
   * if a node was inserted. If a node for IP, mask already existed
   * in the tree we return that node, false.
   * Additional template parameter VALUE is just to facilitated
   * universal references as inside the body of RadixTree T is
   * not a deduced type.
   */
  template <typename VALUE>
  std::pair<Iterator, bool>
  insert(const IPADDRTYPE& ipaddr, uint8_t masklen, VALUE&& value);

  // Erase a IP, mask
  bool erase(const IPADDRTYPE& ipaddr, uint8_t masklen) {
    return erase(exactMatch(ipaddr, masklen));
  }

  // Erase node pointed to be iterator
  bool erase(Iterator itr) {
    if (itr == end()) {
      return false;
    }
    return erase(&(*itr));
  }

  // Erase a node from Radix trees.
  bool erase(TreeNode* node);

  // Given a IP, mask return the node with longest match for it
  // NOTE: masklen is unsigned and must be <= ipaddr.bitCount()
  ConstIterator longestMatch(const IPADDRTYPE& ipaddr, uint8_t masklen) const {
    auto foundExact = false;
    return traits_.makeCItr(longestMatchImpl(ipaddr, masklen, foundExact));
  }

  // Non const longest match
  Iterator longestMatch(const IPADDRTYPE& ipaddr, uint8_t mask) {
    return traits_.itrConstCast(
        const_cast<const RadixTree*>(this)->longestMatch(ipaddr, mask));
  }

  /*
   * Given a IP, mask return node whose IP, mask which matches this prefix
   * exactly
   */
  ConstIterator exactMatch(const IPADDRTYPE& ipaddr, uint8_t masklen) const {
    auto foundExact = false;
    auto match = longestMatchImpl(ipaddr, masklen, foundExact);
    return traits_.makeCItr(foundExact ? match : nullptr);
  }

  // Non const exact match
  Iterator exactMatch(const IPADDRTYPE& ipaddr, uint8_t mask) {
    return traits_.itrConstCast(
        const_cast<const RadixTree*>(this)->exactMatch(ipaddr, mask));
  }

  /*
   * Get longest match as with the longestMatch api, but in addition record
   * the path from root to this node. Boolean parameter to control whether
   * we consider non value nodes when evaluating longest match and whether
   * we insert these nodes in the tree.
   * Passed in trail vector is modified only on a successful match, at which
   * point it contains the nodes seen on path from root to the matching node.
   * includeNonValueNodes boolean controls whether non value nodes are
   * considered for prefix matching and insertion into trail vector.
   */
  ConstIterator longestMatchWithTrail(
      const IPADDRTYPE& ipaddr,
      uint8_t masklen,
      VecConstIterators& trail,
      bool includeNonValueNodes = false) const {
    auto foundExact = false;
    VecConstIterators trailInternal;
    trailInternal.reserve(IPADDRTYPE::bitCount());
    auto longestMatchNode = longestMatchImpl(
        ipaddr, masklen, foundExact, includeNonValueNodes, &trailInternal);
    if (longestMatchNode) {
      trail.swap(trailInternal);
    }
    return traits_.makeCItr(longestMatchNode, includeNonValueNodes);
  }

  // Non const longestMatchWithTrail
  Iterator longestMatchWithTrail(
      const IPADDRTYPE& ipaddr,
      uint8_t masklen,
      VecConstIterators& trail,
      bool includeNonValueNodes = false) {
    return traits_.itrConstCast(
        const_cast<const RadixTree*>(this)->longestMatchWithTrail(
            ipaddr, masklen, trail, includeNonValueNodes));
  }

  /*
   * Same as longestMatchWithTrail, but returns a node, trail only if there
   * is a exact match. Again boolean parameter to control if non value
   * nodes are considered in match, trail.
   */
  ConstIterator exactMatchWithTrail(
      const IPADDRTYPE& ipaddr,
      uint8_t masklen,
      VecConstIterators& trail,
      bool includeNonValueNodes = false) const {
    auto foundExact = false;
    VecConstIterators trailInternal;
    trailInternal.reserve(IPADDRTYPE::bitCount());
    auto exactMatchNode = longestMatchImpl(
        ipaddr, masklen, foundExact, includeNonValueNodes, &trailInternal);
    if (foundExact) {
      trail.swap(trailInternal);
      return traits_.makeCItr(exactMatchNode, includeNonValueNodes);
    }
    return traits_.makeCItr(nullptr, includeNonValueNodes);
  }

  // Non const counterpart of exactMatchWithTrail
  Iterator exactMatchWithTrail(
      const IPADDRTYPE& ipaddr,
      uint8_t masklen,
      VecConstIterators& trail,
      bool includeNonValueNodes = false) {
    return traits_.itrConstCast(
        const_cast<const RadixTree*>(this)->exactMatchWithTrail(
            ipaddr, masklen, trail, includeNonValueNodes));
  }

  // Compare 2 radix (sub) trees
  static bool radixSubTreesEqual(const TreeNode* nodeA, const TreeNode* nodeB);

  // Equality
  bool operator==(const RadixTree& r) const {
    return size_ == r.size_ && radixSubTreesEqual(root(), r.root());
  }

  // Inequality
  bool operator!=(const RadixTree& r) const {
    return !(*this == r);
  }

  size_t size() const {
    return size_;
  }
  const TreeNode* root() const {
    return root_.get();
  }
  TreeNode* root() {
    return root_.get();
  }
  NodeDeleteCallback nodeDeleteCallback() const {
    return nodeDeleteCallback_;
  }
  const TreeTraits& traits() const {
    return traits_;
  }

 private:
  static std::unique_ptr<TreeNode> cloneSubTree(const TreeNode* node);
  // Worker function to do the actual longest match lookup.
  const TreeNode* longestMatchImpl(
      const IPADDRTYPE& ipaddr,
      uint8_t masklen,
      bool& foundExact,
      bool includeNonValueNodes = false,
      VecConstIterators* trail = nullptr) const;

  // Non const longest match lookup
  TreeNode* longestMatchImpl(
      const IPADDRTYPE& ipaddr,
      uint8_t masklen,
      bool& foundExact,
      bool includeNonValueNodes = false,
      VecConstIterators* trail = nullptr) {
    return const_cast<TreeNode*>(
        const_cast<const RadixTree*>(this)->longestMatchImpl(
            ipaddr, masklen, foundExact, includeNonValueNodes, trail));
  }

  std::unique_ptr<TreeNode> makeNode(const IPADDRTYPE& ip, uint8_t masklen) {
    return std::make_unique<TreeNode>(ip, masklen, nodeDeleteCallback_);
  }

  template <typename VALUE>
  std::unique_ptr<TreeNode>
  makeNode(const IPADDRTYPE& ip, uint8_t masklen, VALUE&& value) {
    return std::make_unique<TreeNode>(
        ip, masklen, std::forward<VALUE>(value), nodeDeleteCallback_);
  }

  void makeRoot(std::unique_ptr<TreeNode> newRoot) {
    CHECK(root_ != newRoot || root_ == nullptr);
    if (newRoot) {
      newRoot->setParent(nullptr);
    }
    root_ = std::move(newRoot);
  }

  inline void trailAppend(
      VecConstIterators* trail,
      bool includeNonValueNodes,
      const TreeNode* node) const;

  std::unique_ptr<TreeNode> root_{nullptr};
  size_t size_{0};
  NodeDeleteCallback nodeDeleteCallback_;
  TreeTraits traits_;
};

// RadixTreeIteratorImpl for IPAddress
template <
    typename T,
    typename V4ITRTYPE,
    typename V6ITRTYPE,
    typename DESIREDITERTYPE>
class IPAddressRadixTreeIteratorImpl
    : public std::iterator<DESIREDITERTYPE, T> {
 public:
  using Iterator4 = V4ITRTYPE;
  using Iterator6 = V6ITRTYPE;
  using TreeNode4 = typename V4ITRTYPE::TreeNode;
  using TreeNode6 = typename V6ITRTYPE::TreeNode;
  using ValueType = DESIREDITERTYPE;

  // default constructor
  IPAddressRadixTreeIteratorImpl() {}
  explicit IPAddressRadixTreeIteratorImpl(
      TreeNode4* root4,
      TreeNode6* root6,
      bool includeNonValNodes = false)
      : iterator4_(root4, includeNonValNodes),
        iterator6_(root6, includeNonValNodes) {}

  DESIREDITERTYPE& operator++() {
    checkDereference();
    if (!iterator4_.atEnd()) {
      ++iterator4_;
    } else {
      ++iterator6_;
    }
    return static_cast<DESIREDITERTYPE&>(*this);
  }

  DESIREDITERTYPE operator++(int) {
    DESIREDITERTYPE tmp(static_cast<DESIREDITERTYPE&>(*this));
    ++(*this);
    return tmp;
  }

  // Returns iterator over subtree of current node.
  DESIREDITERTYPE subTreeIterator() const {
    DESIREDITERTYPE tmp(static_cast<const DESIREDITERTYPE&>(*this));
    if (!tmp.iterator4().atEnd()) {
      tmp.iterator4() = tmp.iterator4().subTreeIterator();
      tmp.iterator6().reset();
    } else if (!tmp.iterator6().atEnd()) {
      tmp.iterator6() = tmp.iterator6().subTreeIterator();
    }
    return tmp;
  }

  friend bool operator==(
      DESIREDITERTYPE const& lhs,
      DESIREDITERTYPE const& rhs) {
    return equal(lhs, rhs);
  }
  friend bool operator!=(
      DESIREDITERTYPE const& lhs,
      DESIREDITERTYPE const& rhs) {
    return !(lhs == rhs);
  }

  const Iterator4& iterator4() const {
    return iterator4_;
  }
  const Iterator6& iterator6() const {
    return iterator6_;
  }
  /*
   * The following 2 accessors are for internal use in this
   * class only and written to get around clang complaining
   * about accessing private members of another object of this
   * class from member functions.
   */
  Iterator4& iterator4() {
    return iterator4_;
  }
  Iterator6& iterator6() {
    return iterator6_;
  }

  const DESIREDITERTYPE& operator*() const {
    checkDereference();
    return static_cast<const DESIREDITERTYPE&>(*this);
  }

  const DESIREDITERTYPE* operator->() const {
    checkDereference();
    return static_cast<const DESIREDITERTYPE*>(this);
  }

  DESIREDITERTYPE& operator*() {
    checkDereference();
    return static_cast<DESIREDITERTYPE&>(*this);
  }

  DESIREDITERTYPE* operator->() {
    checkDereference();
    return static_cast<DESIREDITERTYPE*>(this);
  }

  void reset() {
    iterator4_.reset();
    iterator6_.reset();
  }

  bool atEnd() const {
    return iterator4_.atEnd() && iterator6_.atEnd();
  }

  T& value() const {
    checkDereference();
    return const_cast<T&>(
        !iterator4_.atEnd() ? iterator4_->value() : iterator6_->value());
  }

  folly::IPAddress ipAddress() const {
    checkDereference();
    if (!iterator4_.atEnd()) {
      return folly::IPAddress(iterator4_->ipAddress());
    }
    return folly::IPAddress(iterator6_->ipAddress());
  }

  uint8_t masklen() const {
    checkDereference();
    return !iterator4_.atEnd() ? iterator4_->masklen() : iterator6_->masklen();
  }

  std::string str(bool printValue = true) const {
    checkDereference();
    return !iterator4_.atEnd() ? iterator4_->str(printValue)
                               : iterator6_->str(printValue);
  }

  TreeNode4* node4() const {
    return &const_cast<TreeNode4&>(*iterator4_);
  }
  TreeNode6* node6() const {
    return &const_cast<TreeNode6&>(*iterator6_);
  }
  bool includeNonValueNodes() const {
    return iterator4_.includeNonValueNodes();
  }

  // Internal accessor for iterator4_ and iterator6_ nodes. These are meant to
  // be used for functions that need a null pointer when the iterator points to
  // the end of the tree.
  TreeNode4* node4Unchecked() const {
    return iterator4_.atEnd() ? nullptr : node4();
  }
  TreeNode6* node6Unchecked() const {
    return iterator6_.atEnd() ? nullptr : node6();
  }

 protected:
  void checkDereference() const {
    CHECK(!atEnd());
  }
  void checkValueNode() const {
    if (!iterator4_.atEnd()) {
      iterator4_.checkValueNode();
    } else {
      iterator6_.checkValueNode();
    }
  }
  Iterator4 iterator4_{nullptr};
  Iterator6 iterator6_{nullptr};

  template <typename U>
  friend class V4TreeInCompositeTreeTraits;
  template <typename U>
  friend class V6TreeInCompositeTreeTraits;
  template <typename IPADDRTYPE, typename U, typename TreeTraits>
  friend class RadixTree;

 private:
  static bool equal(DESIREDITERTYPE const& lhs, DESIREDITERTYPE const& rhs) {
    return lhs.iterator4() == rhs.iterator4() &&
        lhs.iterator6() == rhs.iterator6();
  }
};

// Template specialization of RadixTreeIterator for IPAddress
template <typename T>
class RadixTreeIterator<folly::IPAddress, T>
    : public IPAddressRadixTreeIteratorImpl<
          T,
          RadixTreeIterator<folly::IPAddressV4, T>,
          RadixTreeIterator<folly::IPAddressV6, T>,
          RadixTreeIterator<folly::IPAddress, T>> {
 public:
  using IteratorImpl = IPAddressRadixTreeIteratorImpl<
      T,
      RadixTreeIterator<folly::IPAddressV4, T>,
      RadixTreeIterator<folly::IPAddressV6, T>,
      RadixTreeIterator<folly::IPAddress, T>>;
  using TreeNode4 = typename IteratorImpl::TreeNode4;
  using TreeNode6 = typename IteratorImpl::TreeNode6;

 private:
  using IteratorImpl::checkDereference;
  using IteratorImpl::checkValueNode;
  using IteratorImpl::iterator4_;
  using IteratorImpl::iterator6_;

 public:
  // Inherit constructors
  using IteratorImpl::IteratorImpl;

  template <typename VALUE>
  void setValue(VALUE&& value) {
    checkDereference();
    checkValueNode();
    !iterator4_.atEnd() ? iterator4_->setValue(std::forward<VALUE>(value))
                        : iterator6_->setValue(std::forward<VALUE>(value));
  }
};

// Template specialization of RadixTreeConstIterator for IPAddress
template <typename T>
class RadixTreeConstIterator<folly::IPAddress, T>
    : public IPAddressRadixTreeIteratorImpl<
          const T,
          RadixTreeConstIterator<folly::IPAddressV4, T>,
          RadixTreeConstIterator<folly::IPAddressV6, T>,
          RadixTreeConstIterator<folly::IPAddress, T>> {
 public:
  using IteratorImpl = IPAddressRadixTreeIteratorImpl<
      const T,
      RadixTreeConstIterator<folly::IPAddressV4, T>,
      RadixTreeConstIterator<folly::IPAddressV6, T>,
      RadixTreeConstIterator<folly::IPAddress, T>>;
  using TreeNode4 = typename IteratorImpl::TreeNode4;
  using TreeNode6 = typename IteratorImpl::TreeNode6;
  using NonConstIterator = RadixTreeIterator<folly::IPAddress, T>;
  // Inherit constructors
  using IteratorImpl::IteratorImpl;
  // default constructor
  RadixTreeConstIterator() {}
  explicit RadixTreeConstIterator(NonConstIterator itr)
      : RadixTreeConstIterator(
            itr.node4Unchecked(),
            itr.node6Unchecked(),
            itr.includeNonValueNodes()) {}
};

/*
 * Iterator traits for V6 tree embedded inside a (composite V4 and V6)
 * IPAddress tree
 */
template <typename T>
struct V6TreeInCompositeTreeTraits {
  using Iterator = RadixTreeIterator<folly::IPAddress, T>;
  using ConstIterator = RadixTreeConstIterator<folly::IPAddress, T>;
  using TreeNode4 = RadixTreeNode<folly::IPAddressV4, T>;
  using TreeNode6 = RadixTreeNode<folly::IPAddressV6, T>;

  Iterator itrConstCast(ConstIterator citr) const {
    return Iterator(
        const_cast<TreeNode4*>(citr.node4Unchecked()),
        const_cast<TreeNode6*>(citr.node6Unchecked()),
        citr.includeNonValueNodes());
  }

  Iterator makeItr(TreeNode6* node, bool includeNonValueNodes = false) const {
    return Iterator(nullptr, node, includeNonValueNodes);
  }

  ConstIterator makeCItr(
      const TreeNode6* node,
      bool includeNonValueNodes = false) const {
    return ConstIterator(nullptr, node, includeNonValueNodes);
  }
};

/*
 * Traits for V4 tree embedded inside a (composite V4 and V6)
 * IPAddress tree
 */
template <typename T>
struct V4TreeInCompositeTreeTraits {
  using Iterator = RadixTreeIterator<folly::IPAddress, T>;
  using ConstIterator = RadixTreeConstIterator<folly::IPAddress, T>;
  using TreeNode6 = RadixTreeNode<folly::IPAddressV6, T>;
  using TreeNode4 = RadixTreeNode<folly::IPAddressV4, T>;

  using Tree6 =
      RadixTree<folly::IPAddressV6, T, V6TreeInCompositeTreeTraits<T>>;
  explicit V4TreeInCompositeTreeTraits(Tree6& v6Tree) : v6Tree_(v6Tree) {}

  Iterator itrConstCast(ConstIterator citr) const {
    // Note: since citr might be at the end and thus dereferencing
    // it via -> or * might throw, use the . operator to access needed
    // api. Alternatively we could check for citr being at end and
    // create the Iterator appropriately, however the following is
    // both more convenient and efficient.
    return Iterator(
        const_cast<TreeNode4*>(citr.node4Unchecked()),
        const_cast<TreeNode6*>(citr.node6Unchecked()),
        citr.includeNonValueNodes());
  }

  Iterator makeItr(TreeNode4* node, bool includeNonValueNodes = false) const {
    return Iterator(node, v6Tree_.root(), includeNonValueNodes);
  }

  ConstIterator makeCItr(
      const TreeNode4* node,
      bool includeNonValueNodes = false) const {
    return ConstIterator(node, v6Tree_.root(), includeNonValueNodes);
  }
  /*
   * Allow passing a custom root6, needed for find methods
   * where on not finding a v4 prefix in the composite tree we
   * want to return end() rather than a iterator pointing to
   * the beginning of v6 Tree
   */
  Iterator makeItr(
      TreeNode4* node,
      TreeNode6* root6,
      bool includeNonValueNodes = false) const {
    return Iterator(node, root6, includeNonValueNodes);
  }

  /*
   * Allow passing a custom root6, needed for find methods
   * where on not finding a v4 prefix in the composite tree we
   * want to return end() rather than a iterator pointing to
   * the beginning of v6 Tree
   */
  ConstIterator makeCItr(
      const TreeNode4* node,
      const TreeNode6* root6,
      bool includeNonValueNodes = false) const {
    return ConstIterator(node, root6, includeNonValueNodes);
  }

 private:
  Tree6& v6Tree_;
};

// Template specialization for RadixTree of folly::IPAddress
template <typename T>
class RadixTree<folly::IPAddress, T> {
 public:
  using TreeNode4 = RadixTreeNode<folly::IPAddressV4, T>;
  using TreeNode6 = RadixTreeNode<folly::IPAddressV6, T>;
  using NodeDeleteCallback4 = typename TreeNode4::NodeDeleteCallback;
  using NodeDeleteCallback6 = typename TreeNode6::NodeDeleteCallback;
  using Iterator = RadixTreeIterator<folly::IPAddress, T>;
  using ConstIterator = RadixTreeConstIterator<folly::IPAddress, T>;
  using VecConstIterators = std::vector<ConstIterator>;

  explicit RadixTree(
      NodeDeleteCallback4 nodeDeleteCallback4 = NodeDeleteCallback4(),
      NodeDeleteCallback6 nodeDeleteCallback6 = NodeDeleteCallback6())
      : ipv6Tree_(nodeDeleteCallback6, V6TreeInCompositeTreeTraits<T>()),
        ipv4Tree_(
            nodeDeleteCallback4,
            V4TreeInCompositeTreeTraits<T>(ipv6Tree_)) {}

  RadixTree(RadixTree&& r) noexcept
      : RadixTree(
            r.ipv4Tree_.nodeDeleteCallback(),
            r.ipv6Tree_.nodeDeleteCallback()) {
    *this = std::move(r);
  }
  RadixTree& operator=(RadixTree&& r) noexcept {
    ipv6Tree_ = std::move(r.ipv6Tree_);
    ipv4Tree_ = std::move(r.ipv4Tree_);
    return r;
  }
  RadixTree(const RadixTree& r) = delete;
  RadixTree& operator=(const RadixTree& r) = delete;

  bool operator==(const RadixTree& r) const {
    return ipv4Tree_ == r.ipv4Tree_ && ipv6Tree_ == r.ipv6Tree_;
  }

  template <typename U = T>
  typename std::enable_if<std::is_copy_constructible<U>::value, RadixTree>::type
  clone() const {
    static_assert(
        std::is_same<T, U>::value,
        "clone template type must be the same as Radix tree value type");
    RadixTree r(ipv4Tree_.nodeDeleteCallback(), ipv6Tree_.nodeDeleteCallback());
    r.ipv4Tree_ = ipv4Tree_.clone();
    r.ipv6Tree_ = ipv6Tree_.clone();
    return r;
  }

  bool operator!=(const RadixTree& r) const {
    return !(*this == r);
  }

  /*
   * Iteration for the combined tree begins at V4 tree and then
   * rolls on to V6 tree.
   */
  Iterator begin() {
    return ipv4Tree_.begin();
  }
  Iterator end() {
    return ipv6Tree_.end();
  }
  ConstIterator begin() const {
    return ipv4Tree_.begin();
  }
  ConstIterator end() const {
    return ipv6Tree_.end();
  }

  // Free all nodes and clear the tree.
  void clear() {
    ipv4Tree_.clear();
    ipv6Tree_.clear();
  }

  size_t size() const {
    return ipv4Tree_.size() + ipv6Tree_.size();
  }
  size_t size4() const {
    return ipv4Tree_.size();
  }
  size_t size6() const {
    return ipv6Tree_.size();
  }
  /*
   * Insert a IP, mask, value in tree. Returns inserted node, true
   * if a node was inserted. If a node for IP, mask already existed
   * in the tree we return that node, false.
   */
  template <typename VALUE>
  std::pair<Iterator, bool>
  insert(const folly::IPAddress& ipaddr, uint8_t masklen, VALUE&& value) {
    if (ipaddr.isV4()) {
      return ipv4Tree_.insert(
          ipaddr.asV4(), masklen, std::forward<VALUE>(value));
    }
    return ipv6Tree_.insert(ipaddr.asV6(), masklen, std::forward<VALUE>(value));
  }

  // Erase a IP, mask
  bool erase(const folly::IPAddress& ipaddr, uint8_t masklen) {
    if (ipaddr.isV4()) {
      auto itr = exactMatch(ipaddr, masklen);
      return ipv4Tree_.erase(itr.node4Unchecked());
    }
    auto itr = exactMatch(ipaddr, masklen);
    return ipv6Tree_.erase(itr.node6Unchecked());
  }

  // Erase node pointed to be iterator
  bool erase(Iterator itr) {
    return erase(itr->ipAddress(), itr->masklen());
  }

  // Given a IP, mask return the node with longest match for it
  ConstIterator longestMatch(const folly::IPAddress& ipaddr, uint8_t masklen)
      const {
    if (ipaddr.isV4()) {
      auto itr = ipv4Tree_.longestMatch(ipaddr.asV4(), masklen);
      return itr.node4Unchecked()
          ? itr
          : ipv4Tree_.traits().makeCItr(itr.node4Unchecked(), nullptr);
    }
    return ipv6Tree_.longestMatch(ipaddr.asV6(), masklen);
  }

  // Non const longest match
  Iterator longestMatch(const folly::IPAddress& ipaddr, uint8_t masklen) {
    // Using traits of either v4 or v6 tree would work since all we need
    // is a iterator cast
    return ipv4Tree_.traits().itrConstCast(
        const_cast<const RadixTree*>(this)->longestMatch(ipaddr, masklen));
  }

  /*
   * Given a IP, mask return iterator pointing to an node whose IP, mask
   * match this prefix exactly
   */
  ConstIterator exactMatch(const folly::IPAddress& ipaddr, uint8_t masklen)
      const {
    if (ipaddr.isV4()) {
      auto itr = ipv4Tree_.exactMatch(ipaddr.asV4(), masklen);
      return itr.node4Unchecked()
          ? itr
          : ipv4Tree_.traits().makeCItr(itr.node4Unchecked(), nullptr);
    }
    return ipv6Tree_.exactMatch(ipaddr.asV6(), masklen);
  }

  // Non const exact match
  Iterator exactMatch(const folly::IPAddress& ipaddr, uint8_t masklen) {
    // Using traits of either v4 or v6 tree would work since all we need
    // is a iterator cast
    return ipv4Tree_.traits().itrConstCast(
        const_cast<const RadixTree*>(this)->exactMatch(ipaddr, masklen));
  }

  /*
   * Given a IP, mask return iterator pointing to a node whose IP, mask
   * best match this prefix. Record the trail on route.
   */
  ConstIterator longestMatchWithTrail(
      const folly::IPAddress& ipaddr,
      uint8_t masklen,
      VecConstIterators& trail,
      bool includeNonValueNodes = false) const {
    if (ipaddr.isV4()) {
      auto itr = ipv4Tree_.longestMatchWithTrail(
          ipaddr.asV4(), masklen, trail, includeNonValueNodes);
      return itr.node4Unchecked()
          ? itr
          : ipv4Tree_.traits().makeCItr(itr.node4Unchecked(), nullptr);
    }
    return ipv6Tree_.longestMatchWithTrail(
        ipaddr.asV6(), masklen, trail, includeNonValueNodes);
  }

  // Non const longestMatchWithTrail
  Iterator longestMatchWithTrail(
      const folly::IPAddress& ipaddr,
      uint8_t masklen,
      VecConstIterators& trail,
      bool includeNonValueNodes = false) {
    // Using traits of either v4 or v6 tree would work since all we need
    // is a iterator cast
    return ipv4Tree_.traits().itrConstCast(
        const_cast<const RadixTree*>(this)->longestMatchWithTrail(
            ipaddr, masklen, trail, includeNonValueNodes));
  }

  /*
   * Given a IP, mask return iterator pointing to a node whose IP, mask
   * match this prefix exactly. Record the trail on route.
   */
  ConstIterator exactMatchWithTrail(
      const folly::IPAddress& ipaddr,
      uint8_t masklen,
      VecConstIterators& trail,
      bool includeNonValueNodes = false) const {
    if (ipaddr.isV4()) {
      auto itr = ipv4Tree_.exactMatchWithTrail(
          ipaddr.asV4(), masklen, trail, includeNonValueNodes);
      return itr.node4Unchecked()
          ? itr
          : ipv4Tree_.traits().makeCItr(itr.node4Unchecked(), nullptr);
    }
    return ipv6Tree_.exactMatchWithTrail(
        ipaddr.asV6(), masklen, trail, includeNonValueNodes);
  }

  // Non const exactMatchWithTrail
  Iterator exactMatchWithTrail(
      const folly::IPAddress& ipaddr,
      uint8_t masklen,
      VecConstIterators& trail,
      bool includeNonValueNodes = false) {
    // Using traits of either v4 or v6 tree would work since all we need
    // is a iterator cast
    return ipv4Tree_.traits().itrConstCast(
        const_cast<const RadixTree*>(this)->exactMatchWithTrail(
            ipaddr, masklen, trail, includeNonValueNodes));
  }

 private:
  RadixTree<folly::IPAddressV6, T, V6TreeInCompositeTreeTraits<T>> ipv6Tree_;
  RadixTree<folly::IPAddressV4, T, V4TreeInCompositeTreeTraits<T>> ipv4Tree_;
};

// Free standing helper functions

// Given a radix tree iterator get its path from root
template <typename IterType>
typename std::vector<IterType> pathFromRoot(
    IterType citr,
    bool includeNonValueNodes = false);

// Print nodes along a trail
template <typename IterType>
std::string trailStr(
    const typename std::vector<IterType>& trail,
    bool printValues = true);

// Disallow instantiation with folly::IPAddress
template <typename T>
class RadixTreeNode<folly::IPAddress, T>;

} // namespace facebook::network

#include "fboss/lib/RadixTree-inl.h"

#endif // RADIX_TREE_H
