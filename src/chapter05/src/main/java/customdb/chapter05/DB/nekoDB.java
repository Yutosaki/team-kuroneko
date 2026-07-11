package customdb.chapter05.DB;

import java.io.IOException;
import java.io.RandomAccessFile;
import java.nio.ByteBuffer;
import java.nio.charset.StandardCharsets;
import java.nio.file.Files;
import java.nio.file.Path;
import java.util.LinkedList;
import java.util.Queue;
import java.util.Scanner;

import customdb.chapter05.Parser.SimpleParser;

public class nekoDB {
  private static final String DATA_PATH = "data/chapter04.db";

  private static final int PAGE_SIZE = 4096;
  private static final int SLOT_SIZE = 64;
  private static final int MAX_SLOTS = PAGE_SIZE / SLOT_SIZE;

  private Scanner scanner;
  private SimpleParser parser;
  private Path dataPath;
  private RandomAccessFile file;

  private BTree index;
  private Queue<RecordId> freeList;

  public nekoDB() {
    scanner = new Scanner(System.in);
    parser = new SimpleParser();
    dataPath = Path.of(DATA_PATH);

    index = new BTree(10);
    freeList = new LinkedList<>();

    try {
      if (dataPath.getParent() != null && !Files.exists(dataPath.getParent())) {
        Files.createDirectories(dataPath.getParent());
      }
      this.file = new RandomAccessFile(dataPath.toFile(), "rw");

      buildIndex();
    } catch (IOException e) {
      System.out.println("Failed to initialize database: " + e.getMessage());
      return;
    }
    System.out.println("Welcome to nekoDB!");
  }

  /* =========================================================
   * B-Tree データ構造の実装 (インデックス機構)
   * ========================================================= */

  /** レコードの物理的な位置情報を保持する識別子 (Record ID) */
  static class RecordId {
    int page;
    int slot;

    RecordId(int page, int slot) {
      this.page = page;
      this.slot = slot;
    }
  }

  /** B-tree ノード */
  static class BTreeNode {
    int[] keys; // キー配列(ユーザーID)
    RecordId[] values; // レコードポインタ配列(ユーザー名)
    BTreeNode[] children; // 子ノードポインタ
    int numKeys; // 現在のキー数(現在のノードに何個のキーが入っているか)
    boolean isLeaf; // 葉ノード判定(葉かどうか)

    BTreeNode(int t, boolean isLeaf) {
      this.isLeaf = isLeaf;
      this.keys = new int[2 * t - 1];
      this.values = new RecordId[2 * t - 1];
      this.children = new BTreeNode[2 * t];
      this.numKeys = 0;
    }
  }

  /** B-Tree 本体 (Split / Merge 実装済み) */
  static class BTree {
    BTreeNode root;
    int t; // 最小次数

    BTree(int t) {
      this.root = null;
      this.t = t;
    }

    // --- 検索 (Search) ---
    public RecordId search(int key) {
      return root == null ? null : search(root, key);
    }

    private RecordId search(BTreeNode node, int key) {
      int i = 0;
      while (i < node.numKeys && key > node.keys[i]) i++;

      if (i < node.numKeys && key == node.keys[i]) {
        return node.values[i];
      }
      if (node.isLeaf) return null;
      return search(node.children[i], key);
    }

    // --- 挿入 (Insert) ---
    public void insert(int key, RecordId value) {
      if (root == null) {
        root = new BTreeNode(t, true);
        root.keys[0] = key;
        root.values[0] = value;
        root.numKeys = 1;
      } else {
        if (root.numKeys == 2 * t - 1) {
          BTreeNode s = new BTreeNode(t, false);
          s.children[0] = root;
          splitChild(s, 0, root); // ノード分割処理 (Split)

          int i = (s.keys[0] < key) ? 1 : 0;
          insertNonFull(s.children[i], key, value);
          root = s;
        } else {
          insertNonFull(root, key, value);
        }
      }
    }

    private void insertNonFull(BTreeNode node, int key, RecordId value) {
      int i = node.numKeys - 1;
      if (node.isLeaf) {
        while (i >= 0 && node.keys[i] > key) {
          node.keys[i + 1] = node.keys[i];
          node.values[i + 1] = node.values[i];
          i--;
        }
        node.keys[i + 1] = key;
        node.values[i + 1] = value;
        node.numKeys++;
      } else {
        while (i >= 0 && node.keys[i] > key) i--;
        i++;
        if (node.children[i].numKeys == 2 * t - 1) {
          splitChild(node, i, node.children[i]); // 満杯ならSplit
          if (node.keys[i] < key) i++;
        }
        insertNonFull(node.children[i], key, value);
      }
    }

    // ノードの分割 (Split)
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

    // --- 削除 (Delete) ---
    public void delete(int key) {
      if (root == null) return;
      delete(root, key);
      if (root.numKeys == 0) {
        if (root.isLeaf) root = null;
        else root = root.children[0];
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
          fill(node, idx); // マージまたは借用 (Merge/Borrow)
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
      while (idx < node.numKeys && node.keys[idx] < key) idx++;
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
      int k = node.keys[idx];
      if (node.children[idx].numKeys >= t) {
        BTreeNode predNode = node.children[idx];
        while (!predNode.isLeaf) predNode = predNode.children[predNode.numKeys];
        node.keys[idx] = predNode.keys[predNode.numKeys - 1];
        node.values[idx] = predNode.values[predNode.numKeys - 1];
        delete(node.children[idx], node.keys[idx]);
      } else if (node.children[idx + 1].numKeys >= t) {
        BTreeNode succNode = node.children[idx + 1];
        while (!succNode.isLeaf) succNode = succNode.children[0];
        node.keys[idx] = succNode.keys[0];
        node.values[idx] = succNode.values[0];
        delete(node.children[idx + 1], node.keys[idx]);
      } else {
        merge(node, idx);
        delete(node.children[idx], k);
      }
    }

    private void fill(BTreeNode node, int idx) {
      if (idx != 0 && node.children[idx - 1].numKeys >= t) {
        borrowFromPrev(node, idx);
      } else if (idx != node.numKeys && node.children[idx + 1].numKeys >= t) {
        borrowFromNext(node, idx);
      } else {
        if (idx != node.numKeys) merge(node, idx);
        else merge(node, idx - 1);
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
      if (!child.isLeaf) child.children[0] = sibling.children[sibling.numKeys];
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
      if (!child.isLeaf) child.children[child.numKeys + 1] = sibling.children[0];
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

    // ノードのマージ (Merge)
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

  private void buildIndex() throws IOException {
    int numPages = (int) Math.ceil((double) file.length() / PAGE_SIZE);
    int count = 0;

    for (int p = 0; p < numPages; p++) {
      byte[] buf = new byte[PAGE_SIZE];
      file.seek((long) p * PAGE_SIZE);
      file.read(buf);

      for (int s = 0; s < MAX_SLOTS; s++) {
        int offset = s * SLOT_SIZE;
        if (buf[offset] == 1) {
          ByteBuffer bb = ByteBuffer.wrap(buf, offset, SLOT_SIZE);
          bb.get(); // フラグスキップ
          int recordKey = bb.getInt();
          index.insert(recordKey, new RecordId(p, s));
          count++;
        } else {
          freeList.offer(new RecordId(p, s));
        }
      }
    }
    System.out.println("Index build complete. Loaded " + count + " records.");
  }

  public void insert(String key, String value) {
    int id;
    try {
      id = Integer.parseInt(key);
    } catch (NumberFormatException e) {
      System.out.println("Error: Key must be an integer.");
      return;
    }

    // B-treeによる高速な重複チェック O(log N)
    if (index.search(id) != null) {
      System.out.println("Key already exists. Use update command to modify.");
      return;
    }

    try {
      int numPages = (int) Math.ceil((double) file.length() / PAGE_SIZE);

      int targetPage = -1;
      int targetSlot = -1;

      // 線形探索を削除し、freeリストから空きスロットを取得
      if (!freeList.isEmpty()) {
        RecordId freeId = freeList.poll();
        targetPage = freeId.page;
        targetSlot = freeId.slot;
      } else {
        // freeリストが空（既存ページに空きなし）の場合は新規ページ
        targetPage = numPages;
        targetSlot = 0;
        // 新規ページの残りスロットをfreeリストに登録
        for (int s = 1; s < MAX_SLOTS; s++) {
          freeList.offer(new RecordId(targetPage, s));
        }
      }

      byte[] buf = new byte[PAGE_SIZE];
      if (targetPage < numPages) {
        file.seek((long) targetPage * PAGE_SIZE);
        file.read(buf);
      }

      int offset = targetSlot * SLOT_SIZE;
      ByteBuffer bb = ByteBuffer.wrap(buf, offset, SLOT_SIZE);
      bb.put((byte) 1);
      bb.putInt(id);

      byte[] valBytes = value.getBytes(StandardCharsets.UTF_8);
      int len = Math.min(valBytes.length, 55);
      bb.putInt(len);
      bb.put(valBytes, 0, len);

      file.seek((long) targetPage * PAGE_SIZE);
      file.write(buf);

      index.insert(id, new RecordId(targetPage, targetSlot));

      System.out.println("Inserted and saved to disk.");
    } catch (IOException e) {
      System.out.println("Disk I/O Error during insert.");
    }
  }

  // ここは今まで通りの線形探索
  public void select() {
    try {
      int numPages = (int) Math.ceil((double) file.length() / PAGE_SIZE);

      for (int p = 0; p < numPages; p++) {
        byte[] buf = new byte[PAGE_SIZE];
        file.seek((long) p * PAGE_SIZE);
        file.read(buf);

        for (int s = 0; s < MAX_SLOTS; s++) {
          int offset = s * SLOT_SIZE;
          if (buf[offset] == 1) {
            ByteBuffer bb = ByteBuffer.wrap(buf, offset, SLOT_SIZE);
            bb.get();

            int key = bb.getInt();
            int valLen = bb.getInt();
            byte[] valBytes = new byte[valLen];
            bb.get(valBytes);
            String value = new String(valBytes, StandardCharsets.UTF_8);
            System.out.println("(" + key + "," + value + ")");
          }
        }
      }
    } catch (IOException e) {
      System.out.println("Disk I/O Error during select.");
    }
  }

  public void select(String key) {
    int id;
    try {
      id = Integer.parseInt(key);
    } catch (NumberFormatException e) {
      System.out.println("Error: Key must be an integer.");
      return;
    }

    try {
      RecordId rid = index.search(id);
      if (rid == null) {
        System.out.println("Record not found.");
        return;
      }

      byte[] buf = new byte[PAGE_SIZE];
      file.seek((long) rid.page * PAGE_SIZE);
      file.read(buf);

      int offset = rid.slot * SLOT_SIZE;
      ByteBuffer bb = ByteBuffer.wrap(buf, offset, SLOT_SIZE);
      bb.get(); // フラグ
      bb.getInt(); // キー
      int valLen = bb.getInt();
      byte[] valBytes = new byte[valLen];
      bb.get(valBytes);

      System.out.println(new String(valBytes, StandardCharsets.UTF_8));
    } catch (IOException e) {
      System.out.println("Disk I/O Error during select.");
    }
  }

  public void update(String key, String value) {
    int id;
    try {
      id = Integer.parseInt(key);
    } catch (NumberFormatException e) {
      System.out.println("Error: Key must be an integer.");
      return;
    }

    try {
      // B-treeを使って対象レコードを特定
      RecordId rid = index.search(id);
      if (rid == null) {
        System.out.println("Record not found.");
        return;
      }

      // 直接対象ページを読み込み
      byte[] buf = new byte[PAGE_SIZE];
      file.seek((long) rid.page * PAGE_SIZE);
      file.read(buf);

      int offset = rid.slot * SLOT_SIZE;
      ByteBuffer bb = ByteBuffer.wrap(buf, offset, SLOT_SIZE);
      bb.get();
      bb.getInt();

      byte[] valBytes = value.getBytes(StandardCharsets.UTF_8);
      int len = Math.min(valBytes.length, 55);
      bb.putInt(len);
      bb.put(valBytes, 0, len);

      // 対象ページのみ上書き保存
      file.seek((long) rid.page * PAGE_SIZE);
      file.write(buf);
      System.out.println("Updated correctly");

    } catch (IOException e) {
      System.out.println("Disk I/O Error during update.");
    }
  }

  public void delete(String key) {
    int id;
    try {
      id = Integer.parseInt(key);
    } catch (NumberFormatException e) {
      System.out.println("Error: Key must be an integer.");
      return;
    }

    try {
      RecordId rid = index.search(id);
      if (rid == null) {
        System.out.println("Record not found.");
        return;
      }

      // ディスクの論理削除処理
      byte[] buf = new byte[PAGE_SIZE];
      file.seek((long) rid.page * PAGE_SIZE);
      file.read(buf);

      buf[rid.slot * SLOT_SIZE] = 0; // フラグを0に

      file.seek((long) rid.page * PAGE_SIZE);
      file.write(buf);

      // B-treeインデックスからも削除 (ノードのマージ処理等が発生)
      index.delete(id);
      freeList.offer(rid); // 空きスロットとして登録

      System.out.println("Deleted and saved to disk.");
    } catch (IOException e) {
      System.out.println("Disk I/O Error during delete.");
    }
  }

  public void start() {
    while (true) {
      System.out.print("db > ");
      System.out.flush();
      if (!scanner.hasNextLine()) {
        break;
      }

      String[] tokens = parser.parse(scanner.nextLine());
      String command = parser.getCommand(tokens);

      long startTime = System.nanoTime();

      if (command.isEmpty()) {
        continue;
      } else if (command.equals("insert") && tokens.length == 3) {
        insert(tokens[1], tokens[2]);
      } else if (command.equals("select")) {
        if (tokens.length == 1) select();
        else if (tokens.length == 2) select(tokens[1]);
      } else if (command.equals("update") && tokens.length == 3) {
        update(tokens[1], tokens[2]);
      } else if (command.equals("delete") && tokens.length == 2) {
        delete(tokens[1]);
      } else if (command.equals("exit")) {
        try {
          if (file != null) file.close();
        } catch (IOException ignored) {
        }
        System.out.println("Bye!");
        break;
      } else {
        System.out.println("Unknown command");
      }

      long endTime = System.nanoTime();
      double timeMs = (endTime - startTime) / 1_000_000.0;
      System.out.printf("(Executed in %.3f ms)\n", timeMs);
    }
  }
}
