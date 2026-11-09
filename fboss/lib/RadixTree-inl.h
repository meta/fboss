// Copyright 2004-present Facebook. All Rights Reserved.
#ifndef RADIX_TREE_H
#error "This should only be included by RadixTree.h"
#endif

namespace facebook::network {

template <typename IPADDRTYPE, typename T>
typename RadixTreeNode<IPADDRTYPE, T>::TreeDirection
RadixTreeNode<IPADDRTYPE, T>::searchDirection(
    const IPADDRTYPE& toSearch,
    uint8_t toSearchMasklen) const {
  if (masklen_ < toSearchMasklen) {
    // My masklen is less than what is being searched, we are searching
    // a more specific address.
    //
    // Node addresses are always stored pre-masked to their masklen_, so
    // comparing the first masklen_ bits of toSearch against ipAddress_ is
    // equivalent to toSearch.mask(masklen_) == ipAddress_.
    //
    // masklen_ comes from public constructors that accept any uint8_t, so it
    // may exceed the address width. In that case fall back to the checked
    // mask() path (which throws, as the original code did) rather than reading
    // past the byte buffer.
    bool prefixMatches;
    if (masklen_ <= IPADDRTYPE::bitCount()) {
      const auto* toSearchBytes = toSearch.bytes();
      const auto* myBytes = ipAddress_.bytes();
      const uint8_t fullBytes = masklen_ >> 3;
      const uint8_t remBits = masklen_ & 0x7;
      prefixMatches = std::memcmp(toSearchBytes, myBytes, fullBytes) == 0;
      if (prefixMatches && remBits) {
        const uint8_t partialMask = static_cast<uint8_t>(0xFF << (8 - remBits));
        prefixMatches =
            static_cast<uint8_t>(toSearchBytes[fullBytes] & partialMask) ==
            myBytes[fullBytes];
      }
    } else {
      prefixMatches = toSearch.mask(masklen_) == ipAddress_;
    }
    if (prefixMatches) {
      // All the bits up to my bit length match, check the next bit
      // Note that bit lookup is 0 indexed.
      return toSearch.getNthMSBit(masklen_) == 1 ? TreeDirection::RIGHT
                                                 : TreeDirection::LEFT;
    } else {
      // Bits upto my mask len don't match. Go up towards the parent (i.e.
      // towards a less specific prefix) in the hope of getting a match).
      return TreeDirection::PARENT;
    }
  }
  if (masklen_ == toSearchMasklen && ipAddress_ == toSearch) {
    return TreeDirection::THIS_NODE;
  }
  // 2 cases remain.
  // a) Masklen were equal but the addresses did not match
  // b) My mask len was greater than to be searched Prefix
  // In both cases we go up towards the parent.
  return TreeDirection::PARENT;
}

template <typename IPADDRTYPE, typename T, typename TreeTraits>
const typename RadixTree<IPADDRTYPE, T, TreeTraits>::TreeNode*
RadixTree<IPADDRTYPE, T, TreeTraits>::longestMatchImpl(
    const IPADDRTYPE& ipaddr,
    uint8_t masklen,
    bool& foundExact,
    bool includeNonValueNodes,
    VecConstIterators* trail) const {
  // Can't trust the clients to have 0s in all bits after mask length
  const auto toMatch = ipaddr.mask(masklen);

  // Track parent pointer. This is done rather than relying
  // on node having a parent pointer always, to have the longest
  // match code be shared with implementations which don't
  // have a parent pointer
  TreeNode* parent = nullptr;
  TreeNode* lastValueNodeSeen = nullptr;
  auto curNode = root_.get();
  auto done = false;
  while (curNode && !done) {
    auto searchDirection = curNode->searchDirection(toMatch, masklen);
    switch (searchDirection) {
      case TreeDirection::THIS_NODE:
        trailAppend(trail, includeNonValueNodes, curNode);
        lastValueNodeSeen =
            curNode->isValueNode() ? curNode : lastValueNodeSeen;
        foundExact = curNode->isValueNode() || includeNonValueNodes;
        done = true;
        break;
      case TreeDirection::LEFT:
        trailAppend(trail, includeNonValueNodes, curNode);
        lastValueNodeSeen =
            curNode->isValueNode() ? curNode : lastValueNodeSeen;
        if (curNode->left()) {
          parent = curNode;
          curNode = curNode->left();
        } else {
          done = true;
        }
        break;
      case TreeDirection::RIGHT:
        trailAppend(trail, includeNonValueNodes, curNode);
        lastValueNodeSeen =
            curNode->isValueNode() ? curNode : lastValueNodeSeen;
        if (curNode->right()) {
          parent = curNode;
          curNode = curNode->right();
        } else {
          done = true;
        }
        break;
      case TreeDirection::PARENT:
        // We took one extra step in the hope of getting a better
        // match but this didn't succeed. So back up one step
        curNode = parent;
        done = true;
        break;
    }
  }
  return includeNonValueNodes ? curNode : lastValueNodeSeen;
}

template <typename IPADDRTYPE, typename T, typename TreeTraits>
inline void RadixTree<IPADDRTYPE, T, TreeTraits>::trailAppend(
    VecConstIterators* trail,
    bool includeNonValueNodes,
    const TreeNode* node) const {
  if (trail) {
    if (includeNonValueNodes || !node->isNonValueNode()) {
      trail->push_back(traits_.makeCItr(node, includeNonValueNodes));
    }
  }
}

template <typename IPADDRTYPE, typename T, typename TreeTraits>
template <typename VALUE>
std::pair<typename RadixTree<IPADDRTYPE, T, TreeTraits>::Iterator, bool>
RadixTree<IPADDRTYPE, T, TreeTraits>::insert(
    const IPADDRTYPE& ipaddr,
    uint8_t mask,
    VALUE&& value) {
  auto foundExact = false;
  // Can't trust the clients to have 0s in all bits after mask length
  auto toAdd = ipaddr.mask(mask);
  auto bestMatch = longestMatchImpl(
      toAdd, mask, foundExact, true /*include non value nodes*/);
  if (foundExact) {
    // Found exact match. Check if in use
    CHECK_NOTNULL(bestMatch);
    if (bestMatch->isNonValueNode()) {
      bestMatch->setValue(std::forward<VALUE>(value));
      ++size_;
      return std::make_pair(traits_.makeItr(bestMatch), true);
    } else {
      // Prefix already exists in the tree
      return std::make_pair(traits_.makeItr(bestMatch), false);
    }
  }
  auto newNode = makeNode(toAdd, mask, std::forward<VALUE>(value));
  // Cache new node pointer since the unique_ptr maybe moved
  auto newNodeRaw = newNode.get();
  if (!bestMatch) {
    // No match found
    if (!root_) {
      // Empty tree, make this the root
      makeRoot(std::move(newNode));
    } else {
      // The root exists but this ipaddr, mask failed to
      // match even the root->ipaddr/mask. We need a less
      // specific root.
      auto prefix = IPADDRTYPE::longestCommonPrefix(
          {root_->ipAddress(), root_->masklen()}, {toAdd, mask});
      std::unique_ptr<TreeNode> newRoot = nullptr;
      if (prefix.first == toAdd && prefix.second == mask) {
        // To be added node is the new root
        newRoot = std::move(newNode);
      } else {
        // Add new root as a non value internal node
        newRoot = makeNode(prefix.first, prefix.second);
      }
      auto oldRootDirection = newRoot->searchDirection(root_.get());
      CHECK(
          oldRootDirection == TreeDirection::LEFT ||
          oldRootDirection == TreeDirection::RIGHT);
      if (oldRootDirection == TreeDirection::LEFT) {
        newRoot->resetLeft(std::move(root_));
        if (newNode) {
          // new node was not moved to be the new root
          newRoot->resetRight(std::move(newNode));
        }
      } else {
        newRoot->resetRight(std::move(root_));
        if (newNode) {
          newRoot->resetLeft(std::move(newNode));
        }
      }
      makeRoot(std::move(newRoot));
    }
  } else {
    auto toAddDirection = bestMatch->searchDirection(toAdd, mask);
    auto done = false;
    CHECK(
        toAddDirection == TreeDirection::LEFT ||
        toAddDirection == TreeDirection::RIGHT);
    if (toAddDirection == TreeDirection::LEFT) {
      if (!bestMatch->left()) {
        bestMatch->resetLeft(std::move(newNode));
        done = true;
      }
    } else {
      if (!bestMatch->right()) {
        bestMatch->resetRight(std::move(newNode));
        done = true;
      }
    }
    if (!done) {
      TreeNode* bestMatchChild = nullptr;
      if (toAddDirection == TreeDirection::LEFT) {
        bestMatchChild = bestMatch->left();
      } else {
        bestMatchChild = bestMatch->right();
      }
      auto prefix = IPADDRTYPE::longestCommonPrefix(
          {bestMatchChild->ipAddress(), bestMatchChild->masklen()},
          {toAdd, mask});
      // Prefix should not already exist in the tree.
      // The reason for this is that the longest common prefix
      // should be a more specific than bestMatch but less specific
      // than bestMatchChild. The reason for being more specific than
      // bestMatch can be seen from the fact that we were asked to go
      // towards the bestMatch's child when we queried bestMatch for
      // searchDirection of the to be added prefix. The reason that it
      // should be less specific than bestMatchChild is because else
      // we should have found bestMatchChild when we asked for
      // a longest match. Since there are no nodes b/w bestMatch
      // and its child we know that the longest common prefix
      // should not be on tree.
      DCHECK(exactMatch(prefix.first, prefix.second) == end());
      if (prefix.first != toAdd || prefix.second != mask) {
        // We need to insert a non value internal node as a parent of
        // bestMatchChild and new node.
        auto internalNode = makeNode(prefix.first, prefix.second);
        auto internalNodeRaw = internalNode.get();
        std::unique_ptr<TreeNode> oldBestMatchChild = nullptr;
        if (toAddDirection == TreeDirection::LEFT) {
          oldBestMatchChild = bestMatch->resetLeft(std::move(internalNode));
        } else {
          oldBestMatchChild = bestMatch->resetRight(std::move(internalNode));
        }
        auto newNodeDirection = internalNodeRaw->searchDirection(newNode.get());
        CHECK(
            newNodeDirection == TreeDirection::LEFT ||
            newNodeDirection == TreeDirection::RIGHT);
        if (newNodeDirection == TreeDirection::LEFT) {
          internalNodeRaw->resetLeft(std::move(newNode));
          internalNodeRaw->resetRight(std::move(oldBestMatchChild));
        } else {
          internalNodeRaw->resetRight(std::move(newNode));
          internalNodeRaw->resetLeft(std::move(oldBestMatchChild));
        }
        CHECK(internalNode == nullptr);
      } else {
        // New node needs to be inserted  b/w bestMatch and bestMatchChild
        std::unique_ptr<TreeNode> oldBestMatchChild = nullptr;
        if (toAddDirection == TreeDirection::LEFT) {
          oldBestMatchChild = bestMatch->resetLeft(std::move(newNode));
        } else {
          oldBestMatchChild = bestMatch->resetRight(std::move(newNode));
        }
        auto bestMatchChildDirection =
            newNodeRaw->searchDirection(oldBestMatchChild.get());
        DCHECK(
            bestMatchChildDirection == TreeDirection::LEFT ||
            bestMatchChildDirection == TreeDirection::RIGHT);
        if (bestMatchChildDirection == TreeDirection::LEFT) {
          newNodeRaw->resetLeft(std::move(oldBestMatchChild));
        } else {
          newNodeRaw->resetRight(std::move(oldBestMatchChild));
        }
      }
    }
  }
  CHECK(newNode == nullptr);
  ++size_;
  return std::make_pair(traits_.makeItr(newNodeRaw), true);
}

/*
 * One condition that must be true before and after erase
 * is that all non value nodes should have 2 children. Assuming
 * that there have been no erase operations so far, this is
 * clearly true, since non value nodes are created by insert
 * operations and are immediately made the parent of a existing
 * node and the to be inserted node.
 * We need to ensure that this condition is held true after erase
 * as well. Why this is true is explained below for each of the
 * different cases of erase.
 */
template <typename IPADDRTYPE, typename T, typename TreeTraits>
bool RadixTree<IPADDRTYPE, T, TreeTraits>::erase(TreeNode* toDelete) {
  if (!toDelete) {
    return false;
  }
  CHECK(toDelete->isValueNode());
  auto parent = toDelete->parent();
  auto left = toDelete->left();
  auto right = toDelete->right();
  if (left && right) {
    // If we remove toDelete, we would need to insert a new non value
    // toDelete in its place. Since in addition to holding a value, the
    // prefix on this toDelete is the longest common prefix b/w its left
    // and right children.
    // NOTE: Calling makeNonValueNode saves us a deallocation and a allocation.
    // An alternative would have been to destroy the value node and then
    // replace it with a new non value node in tree.
    toDelete->makeNonValueNode();
    // NOTE: Post condition is held. All we did was to make toDelete
    // (which has 2 children), a non value node. So all non value
    // nodes continue to have 2 children.
  } else if (left || right) {
    // toDelete has just one child, let the child's grandparent
    // adopt it since toDelete is about to got away.
    if (parent) {
      // Note - toDelete gets freed here.
      if (parent->left() == toDelete) {
        parent->resetLeft(
            left ? toDelete->resetLeft(nullptr)
                 : toDelete->resetRight(nullptr));
      } else {
        parent->resetRight(
            left ? toDelete->resetLeft(nullptr)
                 : toDelete->resetRight(nullptr));
      }
    } else {
      CHECK(root_.get() == toDelete);
      // Update root, toDelete (old root) gets deleted as a result
      makeRoot(
          left ? toDelete->resetLeft(nullptr) : toDelete->resetRight(nullptr));
    }
    // We just made toDelete's parent the parent of toDelete's only
    // child. There are 2 possibilities with regard to toDelete's parent
    // a) The parent is a value node - In this case there is no bearing
    // on our post condition (we are not touching any non value nodes),
    // which is thus trivially held.
    // b) The parent is a non value node - This means that the parent
    // before this erase operation had 2 children of which the just
    // erased toDelete was one. Since all we did was make the just erased
    // toDelete's only child a child of toDelete's parent, toDelete's
    // parent continues to have 2 children. Post condition is again held.
  } else {
    // toDelete has no children.
    if (parent) {
      // Free toDelete
      parent->left() == toDelete ? parent->resetLeft(nullptr)
                                 : parent->resetRight(nullptr);
      if (parent->isNonValueNode()) {
        // toDelete's parent is a non value node. Since we removed
        // toDelete, toDelete's parent needs to be deleted as well
        // since otherwise we would have a non value node with just
        // one child.
        TreeNode* grandParent = parent->parent();
        // toDeleteSibling must be non null since toDelete's parent
        // was a non value node, which always has 2 children.
        auto toDeleteSibling = parent->left() ? parent->resetLeft(nullptr)
                                              : parent->resetRight(nullptr);
        CHECK(toDeleteSibling);
        if (grandParent) {
          // Free toDelete's parent
          grandParent->left() == parent
              ? grandParent->resetLeft(std::move(toDeleteSibling))
              : grandParent->resetRight(std::move(toDeleteSibling));
          // Here we replaced one of grandparent's children with
          // another and removed parent, toDelete nodes. There are
          // 2 possibilities with regards to grand parent
          // a) Its a value node - So we just replaced a value node's
          // child. This has no bearing on our post
          // condition which is thus trivially held.
          // b) Its a non value node - Thus we just replaced a
          // non value node's child with another. Since we assume
          // that grandparent started with 2 children, it continues
          // to have 2 children and our post condition is held.
        } else {
          // Free parent and set toDeleteSibling to be the root.
          // We got rid of one side of the tree under the old root.
          // Since our starting condition was that the existing
          // radix tree was valid (viz. all non value nodes had
          // 2 children), each subtree of such a tree is also valid.
          // Since the tree under toDeleteSibling is one such tree,
          // our post condition is held.
          CHECK(root_.get() == parent);
          CHECK(parent->isLeaf()); // Both children should be set to null
          makeRoot(std::move(toDeleteSibling));
        }
      } else {
        // toDelete's parent is a value node.
        // Nothing to do. Parent node holds a user inserted value.
        // Post condition holds since all we have done is delete a
        // value node's child and this has no bearing on number of
        // children non value nodes have.
      }
    } else {
      // To be deleted node has no parent and no children.
      // Its thus the root (and only node) in the tree.
      CHECK_EQ(root_.get(), toDelete);
      // Empty tree, post condition trivially held.
      root_.reset(nullptr);
    }
  }
  --size_;
  return true;
}

template <typename IPADDRTYPE, typename T, typename TreeTraits>
bool RadixTree<IPADDRTYPE, T, TreeTraits>::radixSubTreesEqual(
    const TreeNode* nodeA,
    const TreeNode* nodeB) {
  if (nodeA && nodeB) {
    if (nodeA->equalSansLinks(*nodeB)) {
      return radixSubTreesEqual(nodeA->left(), nodeB->left()) &&
          radixSubTreesEqual(nodeA->right(), nodeB->right());
    } else {
      return false;
    }
  }
  return !nodeA && !nodeB;
}

template <typename IPADDRTYPE, typename T, typename TreeTraits>
std::unique_ptr<typename RadixTree<IPADDRTYPE, T, TreeTraits>::TreeNode>
RadixTree<IPADDRTYPE, T, TreeTraits>::cloneSubTree(const TreeNode* node) {
  if (!node) {
    return nullptr;
  }
  std::unique_ptr<TreeNode> copy;
  if (node->isValueNode()) {
    copy = std::make_unique<TreeNode>(
        node->ipAddress(),
        node->masklen(),
        node->value(),
        node->nodeDeleteCallback());
  } else {
    copy = std::make_unique<TreeNode>(
        node->ipAddress(), node->masklen(), node->nodeDeleteCallback());
  }
  copy->resetLeft(cloneSubTree(node->left()));
  copy->resetRight(cloneSubTree(node->right()));
  return copy;
}

template <typename IterType>
typename std::vector<IterType> pathFromRoot(
    IterType itr,
    bool includeNonValueNodes) {
  typename std::vector<IterType> path;
  auto tmp = &(*itr);
  while (tmp) {
    path.push_back(IterType(tmp, includeNonValueNodes));
    tmp = tmp->parent();
  }
  std::reverse(path.begin(), path.end());
  return std::move(path);
}

template <typename IterType>
std::string trailStr(
    const typename std::vector<IterType>& trail,
    bool printValues) {
  std::string path;
  for (auto i = 0; i < trail.size(); ++i) {
    path += trail[i]->str(printValues);
    if (i < trail.size() - 1) {
      path += ", ";
    }
  }
  return path;
}

template <
    typename IPADDRTYPE,
    typename T,
    typename CURSORNODE,
    typename DESIREDITERTYPE>
void RadixTreeIteratorImpl<IPADDRTYPE, T, CURSORNODE, DESIREDITERTYPE>::
    radixTreeItrIncrement() {
  const TreeNode* previous = nullptr;
  auto done = false;
  while (!done && cursor_) {
    if (cursor_ == subTreeEnd_) {
      // We are iterating over sub-tree and we reached the end
      cursor_ = nullptr;
    } else if (!previous || cursor_->parent() == previous) {
      // Going down the tree
      previous = cursor_;
      if (cursor_->left()) {
        cursor_ = cursor_->left();
      } else if (cursor_->right()) {
        cursor_ = cursor_->right();
      } else {
        cursor_ = cursor_->parent();
        continue;
      }
    } else if (cursor_->left() == previous) {
      // Coming up the tree from left.
      previous = cursor_;
      if (cursor_->right()) {
        cursor_ = cursor_->right();
      } else {
        cursor_ = cursor_->parent();
        continue;
      }
    } else if (cursor_->right() == previous) {
      // Coming up the tree from right
      previous = cursor_;
      cursor_ = cursor_->parent();
      continue;
    }
    done =
        (cursor_ == nullptr || includeNonValueNodes_ || cursor_->isValueNode());
  }
  normalize();
}

} // namespace facebook::network
