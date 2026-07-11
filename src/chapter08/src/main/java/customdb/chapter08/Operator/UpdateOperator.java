package customdb.chapter08.Operator;

import java.io.IOException;
import java.nio.charset.StandardCharsets;
import java.util.Iterator;

import customdb.chapter08.DB.Row;
import customdb.chapter08.DB.Schema;
import customdb.chapter08.DB.Table;
import customdb.chapter08.Parser.Statement;

public class UpdateOperator implements Operator {
  private final Table table;
  private final Schema schema;
  private final Statement.Update statement;
  private Iterator<Table.Record> iterator;

  public UpdateOperator(Table table, Schema schema, Statement.Update statement) {
    this.table = table;
    this.schema = schema;
    this.statement = statement;
  }

  @Override
  public void open() throws IOException {
    this.iterator = table.scanRecords().iterator();
  }

  @Override
  public Row next() throws IOException {
    Schema.Column targetColumn = schema.getColumn(statement.columnName());
    if (targetColumn == null) {
      throw new IllegalArgumentException("Unknown column: " + statement.columnName());
    }

    while (iterator != null && iterator.hasNext()) {
      Table.Record record = iterator.next();
      Row row = record.row();

      if (ConditionEvaluator.matches(row, statement.whereCondition())) {
        Object newValue = parseValue(statement.value(), targetColumn);
        row.put(targetColumn.name(), newValue);
        table.update(record.recordId(), row);
        return row;
      }
    }

    return null;
  }

  @Override
  public void close() throws IOException {
    this.iterator = null;
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

  private String stripQuote(String value) {
    if (value.length() >= 2 && value.startsWith("'") && value.endsWith("'")) {
      return value.substring(1, value.length() - 1);
    }
    return value;
  }
}
