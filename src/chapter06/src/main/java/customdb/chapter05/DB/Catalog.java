package customdb.chapter05.DB;

import java.io.BufferedReader;
import java.io.BufferedWriter;
import java.io.IOException;
import java.nio.charset.StandardCharsets;
import java.nio.file.Files;
import java.nio.file.Path;
import java.nio.file.StandardCopyOption;
import java.util.ArrayList;
import java.util.HashMap;
import java.util.List;
import java.util.Map;

public class Catalog {
  private final Path baseDir;
  private final Path catalogPath;

  private final Map<String, Schema> schemas = new HashMap<>();
  private final Map<String, Table> tables = new HashMap<>();

  public Catalog(Path baseDir) throws IOException {
    this.baseDir = baseDir;
    this.catalogPath = baseDir.resolve("catalog.txt");

    if (!Files.exists(baseDir)) {
      Files.createDirectories(baseDir);
    }

    load();
  }

  public void createTable(Schema schema) throws IOException {
    String tableName = normalize(schema.getTableName());

    if (schemas.containsKey(tableName)) {
      throw new IllegalArgumentException("Table already exists: " + schema.getTableName());
    }

    Path tablePath = getTablePath(tableName);

    Table table = new Table(schema, tablePath);

    // 新規作成時は既存データを消して初期化
    table.truncate();

    schemas.put(tableName, schema);
    tables.put(tableName, table);

    save();
  }

  public Schema getSchema(String tableName) {
    return schemas.get(normalize(tableName));
  }

  public Table getTable(String tableName) {
    return tables.get(normalize(tableName));
  }

  public Schema requireSchema(String tableName) {
    Schema schema = getSchema(tableName);

    if (schema == null) {
      throw new IllegalArgumentException("Table does not exist: " + tableName);
    }

    return schema;
  }

  public Table requireTable(String tableName) {
    Table table = getTable(tableName);

    if (table == null) {
      throw new IllegalArgumentException("Table does not exist: " + tableName);
    }

    return table;
  }

  public void close() throws IOException {
    IOException firstException = null;

    for (Table table : tables.values()) {
      try {
        table.close();
      } catch (IOException e) {
        if (firstException == null) {
          firstException = e;
        } else {
          firstException.addSuppressed(e);
        }
      }
    }

    if (firstException != null) {
      throw firstException;
    }
  }

  private void load() throws IOException {
    if (!Files.exists(catalogPath)) {
      return;
    }

    try (BufferedReader reader = Files.newBufferedReader(catalogPath, StandardCharsets.UTF_8)) {
      String line;

      while ((line = reader.readLine()) != null) {
        line = line.trim();

        if (line.isEmpty()) {
          continue;
        }

        Schema schema = parseSchemaLine(line);
        String tableName = normalize(schema.getTableName());

        Path tablePath = getTablePath(tableName);
        Table table = new Table(schema, tablePath);

        schemas.put(tableName, schema);
        tables.put(tableName, table);
      }
    }
  }

  private void save() throws IOException {
    if (!Files.exists(baseDir)) {
      Files.createDirectories(baseDir);
    }

    Path tempPath = baseDir.resolve("catalog.txt.tmp");
    try (BufferedWriter writer = Files.newBufferedWriter(tempPath, StandardCharsets.UTF_8)) {
      for (Schema schema : schemas.values()) {
        writer.write(toSchemaLine(schema));
        writer.newLine();
      }
    }
    // 書き込み完了後にアトミックにリネームして上書き
    Files.move(
        tempPath, catalogPath, StandardCopyOption.REPLACE_EXISTING, StandardCopyOption.ATOMIC_MOVE);
  }

  private String toSchemaLine(Schema schema) {
    List<String> columnTexts = new ArrayList<>();

    for (Schema.Column column : schema.getColumns()) {
      String columnText = column.name() + ":" + column.type().name() + ":" + column.length();

      columnTexts.add(columnText);
    }

    return schema.getTableName() + "|" + String.join(",", columnTexts);
  }

  private Schema parseSchemaLine(String line) {
    String[] parts = line.split("\\|", 2);

    if (parts.length != 2) {
      throw new IllegalArgumentException("Invalid catalog line: " + line);
    }

    String tableName = parts[0];
    String columnsPart = parts[1];

    List<Schema.Column> columns = new ArrayList<>();

    if (!columnsPart.isBlank()) {
      String[] columnTexts = columnsPart.split(",");

      for (String columnText : columnTexts) {
        String[] columnParts = columnText.split(":", 3);

        if (columnParts.length != 3) {
          throw new IllegalArgumentException("Invalid column definition: " + columnText);
        }

        String columnName = columnParts[0];
        Schema.DataType type = Schema.DataType.valueOf(columnParts[1].toUpperCase());
        int length = Integer.parseInt(columnParts[2]);

        columns.add(new Schema.Column(columnName, type, length));
      }
    }

    return new Schema(tableName, columns);
  }

  private Path getTablePath(String tableName) {
    return baseDir.resolve(normalize(tableName) + ".tbl");
  }

  private String normalize(String tableName) {
    return tableName.toLowerCase();
  }
}
