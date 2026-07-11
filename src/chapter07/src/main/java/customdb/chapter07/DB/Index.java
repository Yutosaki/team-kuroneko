package customdb.chapter07.DB;

import java.util.ArrayList;
import java.util.List;

// シンプルに chapter04 の BTree を軽量に移植した実装を内部に持つ
public class Index {

  // B-Tree ノード（Chapter04 から簡略化移植）
  static class BTreeNode {
    int[] keys;
    List<Row>[] values; // 各キーに対する複数行
    BTreeNode[] children;
    int numKeys;
    boolean isLeaf;

    @SuppressWarnings("unchecked")
    BTreeNode(int t, boolean isLeaf) {
      this.isLeaf = isLeaf;
      this.keys = new int[2 * t - 1];
      this.values = (List<Row>[]) new List[2 * t - 1];
      this.children = new BTreeNode[2 * t];
      this.numKeys = 0;
    }
  }

  static class BTree {
    BTreeNode root;
    int t;

    BTree(int t) {
      this.root = null;
      this.t = t;
    }

    public List<Row> search(int key) {
      return root == null ? List.of() : search(root, key);
    }

    private List<Row> search(BTreeNode node, int key) {
      int i = 0;
      while (i < node.numKeys && key > node.keys[i]) i++;

      if (i < node.numKeys && key == node.keys[i]) {
        return node.values[i] == null ? List.of() : node.values[i];
      }
      if (node.isLeaf) return List.of();
      return search(node.children[i], key);
    }

    public void insert(int key, Row row) {
      if (root == null) {
        root = new BTreeNode(t, true);
        root.keys[0] = key;
        List<Row> list = new ArrayList<>();
        list.add(row);
        root.values[0] = list;
        root.numKeys = 1;
      } else {
        if (root.numKeys == 2 * t - 1) {
          BTreeNode s = new BTreeNode(t, false);
          s.children[0] = root;
          splitChild(s, 0, root);

          int i = (s.keys[0] < key) ? 1 : 0;
          insertNonFull(s.children[i], key, row);
          root = s;
        } else {
          insertNonFull(root, key, row);
        }
      }
    }

    private void insertNonFull(BTreeNode node, int key, Row row) {
      int i = node.numKeys - 1;
      if (node.isLeaf) {
        while (i >= 0 && node.keys[i] > key) {
          node.keys[i + 1] = node.keys[i];
          node.values[i + 1] = node.values[i];
          i--;
        }

        if (i >= 0 && node.keys[i] == key) {
          if (node.values[i] == null) node.values[i] = new ArrayList<>();
          node.values[i].add(row);
        } else {
          node.keys[i + 1] = key;
          List<Row> list = new ArrayList<>();
          list.add(row);
          node.values[i + 1] = list;
          node.numKeys++;
        }
      } else {
        while (i >= 0 && node.keys[i] > key) i--;
        i++;
        if (node.children[i].numKeys == 2 * t - 1) {
          splitChild(node, i, node.children[i]);
          if (node.keys[i] < key) i++;
        }
        insertNonFull(node.children[i], key, row);
      }
    }

    private void splitChild(BTreeNode parent, int i, BTreeNode fullChild) {
      BTreeNode newNode = new BTreeNode(t, fullChild.isLeaf);
      newNode.numKeys = t - 1;

      for (int j = 0; j < t - 1; j++) {
        newNode.keys[j] = fullChild.keys[j + t];
        newNode.values[j] = fullChild.values[j + t];
      }

      if (!fullChild.isLeaf) {
        for (int j = 0; j < t; j++) {
          newNode.children[j] = fullChild.children[j + t];
        }
      }
      fullChild.numKeys = t - 1;

      for (int j = parent.numKeys; j >= i + 1; j--) {
        parent.children[j + 1] = parent.children[j];
      }
      parent.children[i + 1] = newNode;

      for (int j = parent.numKeys - 1; j >= i; j--) {
        parent.keys[j + 1] = parent.keys[j];
        parent.values[j + 1] = parent.values[j];
      }

      parent.keys[i] = fullChild.keys[t - 1];
      parent.values[i] = fullChild.values[t - 1];
      parent.numKeys++;
    }

    public void delete(int key) {
      if (root == null) return;

      delete(root, key);
      if (root.numKeys == 0) {
        if (root.isLeaf) {
          root = null;
        } else {
          root = root.children[0];
        }
      }
    }

    private void delete(BTreeNode node, int key) {
      int idx = findKey(node, key);

      if (idx < node.numKeys && node.keys[idx] == key) {
        if (node.isLeaf) {
          removeFromLeaf(node, idx);
        } else {
          removeFromNonLeaf(node, idx);
        }
      } else {
        if (node.isLeaf) return;

        boolean flag = (idx == node.numKeys);
        if (node.children[idx].numKeys < t) {
          fill(node, idx);
        }

        if (flag && idx > node.numKeys) {
          delete(node.children[idx - 1], key);
        } else {
          delete(node.children[idx], key);
        }
      }
    }

    private int findKey(BTreeNode node, int key) {
      int idx = 0;
      while (idx < node.numKeys && node.keys[idx] < key) {
        idx++;
      }
      return idx;
    }

    private void removeFromLeaf(BTreeNode node, int idx) {
      for (int i = idx + 1; i < node.numKeys; i++) {
        node.keys[i - 1] = node.keys[i];
        node.values[i - 1] = node.values[i];
      }
      node.numKeys--;
    }

    private void removeFromNonLeaf(BTreeNode node, int idx) {
      int key = node.keys[idx];

      if (node.children[idx].numKeys >= t) {
        BTreeNode predNode = node.children[idx];
        while (!predNode.isLeaf) {
          predNode = predNode.children[predNode.numKeys];
        }
        node.keys[idx] = predNode.keys[predNode.numKeys - 1];
        node.values[idx] = predNode.values[predNode.numKeys - 1];
        delete(node.children[idx], node.keys[idx]);
      } else if (node.children[idx + 1].numKeys >= t) {
        BTreeNode succNode = node.children[idx + 1];
        while (!succNode.isLeaf) {
          succNode = succNode.children[0];
        }
        node.keys[idx] = succNode.keys[0];
        node.values[idx] = succNode.values[0];
        delete(node.children[idx + 1], node.keys[idx]);
      } else {
        merge(node, idx);
        delete(node.children[idx], key);
      }
    }

    private void fill(BTreeNode node, int idx) {
      if (idx != 0 && node.children[idx - 1].numKeys >= t) {
        borrowFromPrev(node, idx);
      } else if (idx != node.numKeys && node.children[idx + 1].numKeys >= t) {
        borrowFromNext(node, idx);
      } else {
        if (idx != node.numKeys) {
          merge(node, idx);
        } else {
          merge(node, idx - 1);
        }
      }
    }

    private void borrowFromPrev(BTreeNode node, int idx) {
      BTreeNode child = node.children[idx];
      BTreeNode sibling = node.children[idx - 1];

      for (int i = child.numKeys - 1; i >= 0; i--) {
        child.keys[i + 1] = child.keys[i];
        child.values[i + 1] = child.values[i];
      }
      if (!child.isLeaf) {
        for (int i = child.numKeys; i >= 0; i--) {
          child.children[i + 1] = child.children[i];
        }
      }

      child.keys[0] = node.keys[idx - 1];
      child.values[0] = node.values[idx - 1];
      if (!child.isLeaf) {
        child.children[0] = sibling.children[sibling.numKeys];
      }

      node.keys[idx - 1] = sibling.keys[sibling.numKeys - 1];
      node.values[idx - 1] = sibling.values[sibling.numKeys - 1];
      child.numKeys++;
      sibling.numKeys--;
    }

    private void borrowFromNext(BTreeNode node, int idx) {
      BTreeNode child = node.children[idx];
      BTreeNode sibling = node.children[idx + 1];

      child.keys[child.numKeys] = node.keys[idx];
      child.values[child.numKeys] = node.values[idx];
      if (!child.isLeaf) {
        child.children[child.numKeys + 1] = sibling.children[0];
      }

      node.keys[idx] = sibling.keys[0];
      node.values[idx] = sibling.values[0];

      for (int i = 1; i < sibling.numKeys; i++) {
        sibling.keys[i - 1] = sibling.keys[i];
        sibling.values[i - 1] = sibling.values[i];
      }
      if (!sibling.isLeaf) {
        for (int i = 1; i <= sibling.numKeys; i++) {
          sibling.children[i - 1] = sibling.children[i];
        }
      }

      child.numKeys++;
      sibling.numKeys--;
    }

    private void merge(BTreeNode node, int idx) {
      BTreeNode child = node.children[idx];
      BTreeNode sibling = node.children[idx + 1];

      child.keys[t - 1] = node.keys[idx];
      child.values[t - 1] = node.values[idx];

      for (int i = 0; i < sibling.numKeys; i++) {
        child.keys[i + t] = sibling.keys[i];
        child.values[i + t] = sibling.values[i];
      }
      if (!child.isLeaf) {
        for (int i = 0; i <= sibling.numKeys; i++) {
          child.children[i + t] = sibling.children[i];
        }
      }

      for (int i = idx + 1; i < node.numKeys; i++) {
        node.keys[i - 1] = node.keys[i];
        node.values[i - 1] = node.values[i];
      }
      for (int i = idx + 2; i <= node.numKeys; i++) {
        node.children[i - 1] = node.children[i];
      }

      child.numKeys += sibling.numKeys + 1;
      node.numKeys--;
    }
  }

  private final BTree btree;

  public Index() {
    // 適当な最小次数を選択
    this.btree = new BTree(10);
  }

  public void add(Object keyObj, Row row) {
    if (keyObj == null) return;
    int key;
    if (keyObj instanceof Number number) {
      key = number.intValue();
    } else {
      try {
        key = Integer.parseInt(keyObj.toString());
      } catch (NumberFormatException e) {
        return; // 現状は整数キーのみサポート
      }
    }

    btree.insert(key, row);
  }

  public List<Row> search(Object keyObj) {
    if (keyObj == null) return List.of();
    int key;
    if (keyObj instanceof Number number) {
      key = number.intValue();
    } else {
      try {
        key = Integer.parseInt(keyObj.toString());
      } catch (NumberFormatException e) {
        return List.of();
      }
    }
    return btree.search(key);
  }

  public void remove(Object keyObj, Row row) {
    if (keyObj == null) {
      return;
    }

    int key;

    if (keyObj instanceof Number number) {
      key = number.intValue();
    } else {
      try {
        key = Integer.parseInt(keyObj.toString());
      } catch (NumberFormatException e) {
        return;
      }
    }

    List<Row> rows = btree.search(key);

    if (!rows.isEmpty()) {
      rows.remove(row);
      if (rows.isEmpty()) {
        btree.delete(key);
      }
    }
  }
}
