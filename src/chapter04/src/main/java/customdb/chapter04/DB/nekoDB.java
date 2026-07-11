package customdb.chapter04.DB;

import java.io.IOException;
import java.io.RandomAccessFile;
import java.nio.ByteBuffer;
import java.nio.charset.StandardCharsets;
import java.nio.file.Files;
import java.nio.file.Path;
import java.util.Scanner;

import customdb.chapter04.Parser.SimpleParser;

public class nekoDB {
  private static final String DATA_PATH = "data/chapter04.db";

  // ページサイズとスロットの定数
  private static final int PAGE_SIZE = 4096; // 4KB
  private static final int SLOT_SIZE = 64;
  private static final int MAX_SLOTS = PAGE_SIZE / SLOT_SIZE; // 64

  private Scanner scanner;
  private SimpleParser parser;
  private Path dataPath;

  private RandomAccessFile file;

  public nekoDB() {
    scanner = new Scanner(System.in);
    parser = new SimpleParser();
    dataPath = Path.of(DATA_PATH);

    try {
      if (dataPath.getParent() != null && !Files.exists(dataPath.getParent())) {
        Files.createDirectories(dataPath.getParent());
      }
      // 読み書きモードでファイルを開く
      this.file = new RandomAccessFile(dataPath.toFile(), "rw");
    } catch (IOException e) {
      System.out.println("Failed to initialize database: " + e.getMessage());
      return;
    }
    System.out.println("Welcome to nekoDB!");
  }

  public void insert(String key, String value) {
    int id;
    try {
      id = Integer.parseInt(key);
    } catch (NumberFormatException e) {
      System.out.println("Error: Key must be an integer.");
      return;
    }

    try {
      int numPages = (int) Math.ceil((double) file.length() / PAGE_SIZE);

      // 重複チェックをして、空きスロットを検索（全ページ・全スロットをスキャン）
      int targetPage = -1;
      int targetSlot = -1;

      for (int p = 0; p < numPages; p++) {
        byte[] buf = new byte[PAGE_SIZE];
        file.seek((long) p * PAGE_SIZE);
        file.read(buf);

        for (int s = 0; s < MAX_SLOTS; s++) {
          int offset = s * SLOT_SIZE;
          if (buf[offset] == 1) { // 使用中スロット
            ByteBuffer bb = ByteBuffer.wrap(buf, offset, SLOT_SIZE);
            bb.get(); // 先頭の有効かどうかのフラグをスキップ
            if (bb.getInt() == id) {
              System.out.println("Key already exists. Use update command to modify.");
              return;
            }
          } else if (buf[offset] == 0 && targetPage == -1) { // 最初の空きスロットを記録
            targetPage = p;
            targetSlot = s;
          }
        }
      }

      // 空きがない場合は新しいページを用意
      if (targetPage == -1) {
        targetPage = numPages;
        targetSlot = 0;
      }

      // 対象のページを読み込み、レコードをバイト配列に書き込む
      byte[] buf = new byte[PAGE_SIZE];
      if (targetPage < numPages) {
        file.seek((long) targetPage * PAGE_SIZE);
        file.read(buf);
      }

      int offset = targetSlot * SLOT_SIZE;
      ByteBuffer bb = ByteBuffer.wrap(buf, offset, SLOT_SIZE);
      bb.put((byte) 1); // 有効フラグをセット
      bb.putInt(id); // キーをセット

      byte[] valBytes = value.getBytes(StandardCharsets.UTF_8);
      int len = Math.min(valBytes.length, 55); // 最大55バイトに制限
      bb.putInt(len);
      bb.put(valBytes, 0, len);

      // 対象ページだけをファイルに上書き保存
      file.seek((long) targetPage * PAGE_SIZE);
      file.write(buf);

      System.out.println("Inserted and saved to disk.");
    } catch (IOException e) {
      System.out.println("Disk I/O Error during insert.");
    }
  }

  public void select() {
    try {
      int numPages = (int) Math.ceil((double) file.length() / PAGE_SIZE);

      for (int p = 0; p < numPages; p++) {
        byte[] buf = new byte[PAGE_SIZE];
        file.seek((long) p * PAGE_SIZE);
        file.read(buf);

        for (int s = 0; s < MAX_SLOTS; s++) {
          int offset = s * SLOT_SIZE;
          if (buf[offset] == 1) { // 使用中スロットのみ読み取る
            ByteBuffer bb = ByteBuffer.wrap(buf, offset, SLOT_SIZE);
            bb.get(); // フラグスキップ

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
      int numPages = (int) Math.ceil((double) file.length() / PAGE_SIZE);

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

            if (recordKey == id) {
              int valLen = bb.getInt();
              byte[] valBytes = new byte[valLen];
              bb.get(valBytes);
              String value = new String(valBytes, StandardCharsets.UTF_8);
              System.out.println(value);
              return;
            }
          }
        }
      }
      System.out.println("Record not found.");
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
      int numPages = (int) Math.ceil((double) file.length() / PAGE_SIZE);

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

            if (recordKey == id) {
              // 対象を見つけたら同じスロットに上書き
              byte[] valBytes = value.getBytes(StandardCharsets.UTF_8);
              int len = Math.min(valBytes.length, 55);

              bb.putInt(len);
              bb.put(valBytes, 0, len);

              // 対象ページだけを保存
              file.seek((long) p * PAGE_SIZE);
              file.write(buf);

              System.out.println("Updated correctly");
              return;
            }
          }
        }
      }
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
      int numPages = (int) Math.ceil((double) file.length() / PAGE_SIZE);

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

            if (recordKey == id) {
              // 対象を見つけたら、先頭フラグを0にして論理削除
              buf[offset] = 0;

              file.seek((long) p * PAGE_SIZE);
              file.write(buf);

              System.out.println("Deleted and saved to disk.");
              return;
            }
          }
        }
      }
      System.out.println("Record not found.");
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

      // --- 計測開始 ---
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

      // --- 計測終了 ---
      long endTime = System.nanoTime();
      double timeMs = (endTime - startTime) / 1_000_000.0;
      System.out.printf("(Executed in %.3f ms)\n", timeMs);
    }
  }
}
