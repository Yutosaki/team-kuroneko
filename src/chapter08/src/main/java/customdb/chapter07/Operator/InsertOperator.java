package customdb.chapter07.Operator;

import customdb.chapter07.DB.Row;
import customdb.chapter07.DB.Schema;
import customdb.chapter07.DB.Table;
import customdb.chapter07.Parser.Statement;
import java.io.IOException;
import java.nio.charset.StandardCharsets;
import java.util.List;

public class InsertOperator implements Operator {
  private final Table table;
  private final Schema schema;
  private final Statement.Insert statement;
  private boolean executed = false;

  public InsertOperator(Table table, Schema schema, Statement.Insert statement) {
    this.table = table;
    this.schema = schema;
    this.statement = statement;
  }

  @Override
  public void open() throws IOException {}

  @Override
  public Row next() throws IOException {
    if (executed) {
      return null;
    }
    Row row = buildRow(schema, statement);
    table.insert(row);
    executed = true;
    return row;
  }

  @Override
  public void close() throws IOException {}

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
            "Column count does not match value count="
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
          "Column count does not match value count="
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

  private Object parseValue(String rawValue, Schema.Column column) {
    String value = stripQuote(rawValue);
    return switch (column.type()) {
      case INTEGER -> Integer.parseInt(value);
      case FLOAT -> Float.parseFloat(value);
      case DOUBLE -> Double.parseDouble(value);
      case STRING -> {
        // 格納は UTF-8 バイト列なので、char 数ではなくバイト長で上限を判定する
        byte[] bytes = value.getBytes(StandardCharsets.UTF_8);
        if (bytes.length > column.length()) {
          throw new IllegalArgumentException(
              "Value byte length exceeds column length. column="
                  + column.name()
                  + ", length="
                  + column.length()
                  + ", byteLength="
                  + bytes.length
                  + ", value="
                  + value);
        }
        yield value;
      }
    };
  }

  private Object defaultValue(Schema.Column column) {
    return switch (column.type()) {
      case INTEGER -> 0;
      case FLOAT -> 0.0f;
      case DOUBLE -> 0.0d;
      case STRING -> "";
    };
  }

  private String stripQuote(String value) {
    if (value.length() >= 2 && value.startsWith("'") && value.endsWith("'")) {
      return value.substring(1, value.length() - 1);
    }
    return value;
  }
}
