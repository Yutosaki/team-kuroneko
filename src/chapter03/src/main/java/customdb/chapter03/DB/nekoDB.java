package customdb.chapter03.DB;

import java.io.IOException;
import java.nio.file.Files;
import java.nio.file.Path;
import java.util.HashMap;
import java.util.List;
import java.util.Map;
import java.util.Scanner;

import customdb.chapter03.Parser.SimpleParser;

public class nekoDB {
  private static final String DATA_PATH = "data/chapter03.db";
  private Map<Integer, String> db;
  private Scanner scanner;
  private SimpleParser parser;
  private Path dataPath;

  public nekoDB() {
    db = new HashMap<>();
    scanner = new Scanner(System.in);
    parser = new SimpleParser();
    dataPath = Path.of(DATA_PATH);

    if (!Files.exists(dataPath.getParent())) {
      try {
        Files.createDirectories(dataPath.getParent());
      } catch (IOException e) {
        System.out.println("Failed to create data directory.");
      }
    } else {
      loadFromFile();
    }
    System.out.println("Welcome to nekoDB!");
  }

  /** ファイルから key,value 形式のテキストを読み込んでレコードを復元する */
  private void loadFromFile() {
    List<String> lines;
    try {
      lines = Files.readAllLines(dataPath);
    } catch (IOException e) {
      System.out.println("Failed to load database.");
      return;
    }

    for (String line : lines) {
      if (line.isBlank()) {
        continue;
      }
      try {
        String[] parts = line.split(",");
        db.put(Integer.parseInt(parts[0]), parts[1]);
      } catch (ArrayIndexOutOfBoundsException e) {
        System.out.println("Skipped invalid record format: " + line);
      }
    }

    System.out.println("Loaded records from file.");
  }

  /** すべてのレコードを key,value 形式のテキストでファイルに保存する */
  private void saveToFile() {
    try {
      List<String> lines =
          db.entrySet().stream().map(entry -> entry.getKey() + "," + entry.getValue()).toList();
      Files.write(dataPath, lines);
    } catch (IOException e) {
      System.out.println("Failed to save database.");
    }
  }

  public void insert(String key, String value) {
    int id;
    try {
      id = Integer.parseInt(key);
    } catch (NumberFormatException e) {
      System.out.println("Error: Key must be an integer.");
      return;
    }

    if (db.putIfAbsent(id, value) != null) {
      System.out.println("Key already exists. Use update command to modify.");
      return;
    }

    saveToFile();
    System.out.println("Inserted and saved to disk.");
  }

  public void select() {
    db.forEach((id, name) -> System.out.println("(" + id + "," + name + ")"));
  }

  public void select(String key) {
    int id;
    try {
      id = Integer.parseInt(key);
    } catch (NumberFormatException e) {
      System.out.println("Error: Key must be an integer.");
      return;
    }

    if (!db.containsKey(id)) {
      System.out.println("Record not found.");
    } else {
      System.out.println(db.get(id));
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

    if (!db.containsKey(id)) {
      System.out.println("Record not found.");
      return;
    }

    db.put(id, value);
    saveToFile();
    System.out.println("Updated and saved to disk.");
  }

  public void delete(String key) {
    int id;
    try {
      id = Integer.parseInt(key);
    } catch (NumberFormatException e) {
      System.out.println("Error: Key must be an integer.");
      return;
    }

    if (!db.containsKey(id)) {
      System.out.println("Record not found.");
      return;
    }

    db.remove(id);
    saveToFile();
    System.out.println("Deleted and saved to disk.");
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
