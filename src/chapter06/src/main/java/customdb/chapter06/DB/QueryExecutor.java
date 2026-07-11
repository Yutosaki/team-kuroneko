package customdb.chapter06.DB;

import java.io.IOException;
import java.nio.file.Path;
import java.util.ArrayList;
import java.util.List;

import customdb.chapter06.Parser.Statement;
import customdb.chapter06.Parser.Statement.JoinClause;

public class QueryExecutor {
  private final Catalog catalog;

  public QueryExecutor(Path baseDir) throws IOException {
    this.catalog = new Catalog(baseDir);
  }

  public void execute(Statement statement) throws IOException {
    if (statement instanceof Statement.CreateTable createTableStatement) {
      executeCreateTable(createTableStatement);
    } else if (statement instanceof Statement.Insert insertStatement) {
      executeInsert(insertStatement);
    } else if (statement instanceof Statement.Select selectStatement) {
      executeSelect(selectStatement);
    } else if (statement instanceof Statement.Update updateStatement) {
      executeUpdate(updateStatement);
    } else if (statement instanceof Statement.Delete deleteStatement) {
      executeDelete(deleteStatement);
    } else {
      throw new IllegalArgumentException(
          "Unsupported statement: " + statement.getClass().getName());
    }
  }

  private void executeCreateTable(Statement.CreateTable statement) throws IOException {
    // cretateTableのASTからSchemaを作成し、Catalogに登録する
    Schema schema = new Schema(statement.tableName(), statement.columns());
    catalog.createTable(schema);
    System.out.println("Table created: " + statement.tableName());
  }

  private void executeInsert(Statement.Insert statement) throws IOException {

    Schema schema = catalog.requireSchema(statement.tableName());
    Table table = catalog.requireTable(statement.tableName());

    // insertのASTからRowを作成
    Row row = buildRow(schema, statement);

    table.insert(row);

    System.out.println("Inserted into " + statement.tableName() + ": " + row);
  }

  private void executeSelect(Statement.Select statement) throws IOException {
    // From句のテーブルを取得し、スキーマを取得する(JOIN句がある場合は左側)
    Table leftTable = catalog.requireTable(statement.tableName());
    Schema leftSchema = catalog.requireSchema(statement.tableName());

    List<Row> rows = new ArrayList<>();

    for (Row row : leftTable.scan()) {
      if (statement.joinClause() == null) {
        rows.add(row);
        continue;
      }
      rows.add(qualifyRow(statement.tableName(), leftSchema, row));
    }

    // JOIN句がある場合は、左側のテーブルの行に対して右側のテーブルを結合する
    boolean hasJoin = statement.joinClause() != null;

    if (hasJoin) {
      rows = executeJoin(rows, statement.joinClause());
    }

    // WHERE句がある場合は、条件に一致する行のみを抽出する
    List<Row> filteredRows = new ArrayList<>();

    for (Row row : rows) {
      if (matches(row, statement.whereCondition())) {
        filteredRows.add(row);
      }
    }

    printRows(filteredRows, statement.selectColumns(), hasJoin);
  }

  private void executeUpdate(Statement.Update statement) throws IOException {
    Schema schema = catalog.requireSchema(statement.tableName());
    Table table = catalog.requireTable(statement.tableName());

    Schema.Column targetColumn = schema.getColumn(statement.columnName());

    if (targetColumn == null) {
      throw new IllegalArgumentException("Unknown column: " + statement.columnName());
    }

    Object newValue = parseValue(statement.value(), targetColumn);

    int updatedCount = 0;

    for (Table.Record record : table.scanRecords()) {
      Row row = record.row();

      if (matches(row, statement.whereCondition())) {
        row.put(targetColumn.name(), newValue);
        table.update(record.recordId(), row);
        updatedCount++;
      }
    }

    System.out.println("Updated " + updatedCount + " row(s).");
  }

  private void executeDelete(Statement.Delete statement) throws IOException {
    Table table = catalog.requireTable(statement.tableName());

    int deletedCount = 0;

    for (Table.Record record : table.scanRecords()) {
      Row row = record.row();

      if (matches(row, statement.whereCondition())) {
        table.delete(record.recordId());
        deletedCount++;
      }
    }

    System.out.println("Deleted " + deletedCount + " row(s).");
  }

  private List<Row> executeJoin(List<Row> leftRows, JoinClause joinClause) throws IOException {
    String rightTableName = joinClause.tableName();

    Table rightTable = catalog.requireTable(rightTableName);
    Schema rightSchema = catalog.requireSchema(rightTableName);

    List<Row> result = new ArrayList<>();

    // Nested Loop Join
    for (Row left : leftRows) {
      for (Row rightRaw : rightTable.scan()) {
        Row right = qualifyRow(rightTableName, rightSchema, rightRaw);

        Row joined = new Row();
        joined.putAll(left);
        joined.putAll(right);

        // JOIN句のON条件に一致する行のみを結果に追加する
        if (matches(joined, joinClause.onCondition())) {
          result.add(joined);
        }
      }
    }

    return result;
  }

  // Insert文からRowを作成
  private Row buildRow(Schema schema, Statement.Insert statement) {
    Row row = new Row();

    for (Schema.Column column : schema.getColumns()) {
      row.put(column.name(), defaultValue(column));
    }

    List<String> columnNames = statement.columnNames();
    List<String> values = statement.values();

    if (columnNames == null || columnNames.isEmpty()) {
      if (values.size() != schema.getColumns().size()) {
        throw new IllegalArgumentException(
            "Column count does not match value count. columns="
                + schema.getColumns().size()
                + ", values="
                + values.size());
      }

      for (int i = 0; i < schema.getColumns().size(); i++) {
        Schema.Column column = schema.getColumns().get(i);
        row.put(column.name(), parseValue(values.get(i), column));
      }

      return row;
    }

    if (columnNames.size() != values.size()) {
      throw new IllegalArgumentException(
          "Column count does not match value count. columns="
              + columnNames.size()
              + ", values="
              + values.size());
    }

    for (int i = 0; i < columnNames.size(); i++) {
      String columnName = columnNames.get(i);
      Schema.Column column = schema.getColumn(columnName);

      if (column == null) {
        throw new IllegalArgumentException("Unknown column: " + columnName);
      }

      row.put(column.name(), parseValue(values.get(i), column));
    }

    return row;
  }

  // Columnを用いて適切な型に変換する
  private Object parseValue(String rawValue, Schema.Column column) {
    String value = stripQuote(rawValue);

    return switch (column.type()) {
      case INTEGER -> Integer.parseInt(value);
      case FLOAT -> Float.parseFloat(value);
      case DOUBLE -> Double.parseDouble(value);
      case STRING -> {
        if (value.length() > column.length()) {
          throw new IllegalArgumentException(
              "Value length exceeds column length. column="
                  + column.name()
                  + ", length="
                  + column.length()
                  + ", value="
                  + value);
        }
        yield value;
      }
    };
  }

  // Columnの型に応じたデフォルト値を返す（insertでvalueの指定がない場合でも初期化される）
  private Object defaultValue(Schema.Column column) {
    return switch (column.type()) {
      case INTEGER -> 0;
      case FLOAT -> 0.0f;
      case DOUBLE -> 0.0d;
      case STRING -> "";
    };
  }

  // 既存の行にテーブル名を付与して、結合時に列名の衝突を避ける(id -> users.id)
  private Row qualifyRow(String tableName, Schema schema, Row row) {
    Row qualified = new Row();

    for (Schema.Column column : schema.getColumns()) {
      String columnName = column.name();
      Object value = row.get(columnName);

      // qualified.put(columnName, value);
      qualified.put(tableName + "." + columnName, value);
    }

    return qualified;
  }

  // 結果の行を出力する
  private void printRows(List<Row> rows, List<String> selectColumns, boolean hasJoin) {
    if (rows.isEmpty()) {
      System.out.println("(empty)");
      return;
    }

    for (Row row : rows) {
      Row projected = projectRow(row, selectColumns, hasJoin);
      System.out.println(projected);
    }
  }

  // SELECT句で指定された列のみを抽出する
  private Row projectRow(Row row, List<String> selectColumns, boolean hasJoin) {
    Row projected = new Row();

    // SELECT * の場合は、すべての列を出力する
    if (selectColumns.size() == 1 && selectColumns.get(0).equals("*")) {
      for (String key : row.keySet()) {
        // JOIN句がある場合はテーブル名付きの列名（users.id）を出力し、ない場合はテーブル名なしの列名を出力する
        if (hasJoin) {
          if (key.contains(".")) {
            projected.put(key, row.get(key));
          }
        } else {
          if (!key.contains(".")) {
            projected.put(key, row.get(key));
          }
        }
      }

      return projected;
    }

    for (String columnName : selectColumns) {
      Object value = resolveColumn(row, columnName);
      projected.put(columnName, value);
    }

    return projected;
  }

  // Rowが条件に一致するかどうかを判定する
  public boolean matches(Row row, Statement.Condition condition) {
    if (condition == null) {
      return true;
    }

    Object leftValue = resolveColumn(row, condition.left());
    Object rightValue = resolveColumnOrLiteral(row, condition.right());

    // 左辺と右辺の値を比較する
    int cmp = compare(leftValue, rightValue);

    return switch (condition.operator()) {
      case "=" -> cmp == 0;
      case "!=" -> cmp != 0;
      case ">" -> cmp > 0;
      case ">=" -> cmp >= 0;
      case "<" -> cmp < 0;
      case "<=" -> cmp <= 0;
      default ->
          throw new IllegalArgumentException("Unsupported operator: " + condition.operator());
    };
  }

  private Object resolveColumn(Row row, String columnName) {
    // 完全一致
    if (row.contains(columnName)) {
      return row.get(columnName);
    }

    // テーブル名付きの列名（users.id）を探す
    String suffix = "." + columnName;
    Object found = null;
    int count = 0;

    for (String key : row.keySet()) {
      if (key.endsWith(suffix)) {
        found = row.get(key);
        count++;
      }
    }

    if (count == 1) {
      return found;
    }

    if (count > 1) {
      throw new IllegalArgumentException("Ambiguous column name: " + columnName);
    }

    throw new IllegalArgumentException("Unknown column: " + columnName);
  }

  // 右辺の値を、リテラル値またはカラム名として解決する（where age >= 20 or join on users.id = orders.user_id）
  private Object resolveColumnOrLiteral(Row row, String value) {
    try {
      return resolveColumn(row, value);
    } catch (IllegalArgumentException ignored) {
      // カラム名ではなくリテラル値として扱う
    }

    String raw = stripQuote(value);

    if (isInteger(raw)) {
      return Integer.parseInt(raw);
    }

    if (isDouble(raw)) {
      return Double.parseDouble(raw);
    }

    return raw;
  }

  private int compare(Object left, Object right) {
    if (left instanceof Number && right instanceof Number) {
      double l = toDouble(left);
      double r = toDouble(right);
      return Double.compare(l, r);
    }

    return left.toString().compareTo(right.toString());
  }

  private double toDouble(Object value) {
    if (value instanceof Number number) {
      return number.doubleValue();
    }

    return Double.parseDouble(value.toString());
  }

  private boolean isInteger(String value) {
    try {
      Integer.parseInt(value);
      return true;
    } catch (NumberFormatException e) {
      return false;
    }
  }

  private boolean isDouble(String value) {
    try {
      Double.parseDouble(value);
      return true;
    } catch (NumberFormatException e) {
      return false;
    }
  }

  // クウォートを削除
  private String stripQuote(String value) {
    if (value.length() >= 2 && value.startsWith("'") && value.endsWith("'")) {
      return value.substring(1, value.length() - 1);
    }

    return value;
  }

  public void close() throws IOException {
    catalog.close();
  }
}
