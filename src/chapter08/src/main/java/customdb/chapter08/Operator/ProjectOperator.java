package customdb.chapter08.Operator;

import java.io.IOException;
import java.util.List;

import customdb.chapter08.DB.Row;

public class ProjectOperator implements Operator {
  private final Operator child;
  private final List<String> selectColumns;
  private final boolean hasJoin;

  public ProjectOperator(Operator child, List<String> selectColumns, boolean hasJoin) {
    this.child = child;
    this.selectColumns = selectColumns;
    this.hasJoin = hasJoin;
  }

  @Override
  public void open() throws IOException {
    child.open();
  }

  @Override
  public Row next() throws IOException {
    Row row = child.next();
    if (row == null) {
      return null;
    }
    return projectRow(row, selectColumns, hasJoin);
  }

  @Override
  public void close() throws IOException {
    child.close();
  }

  private Row projectRow(Row row, List<String> selectColumns, boolean hasJoin) {
    Row projected = new Row();

    if (selectColumns.size() == 1 && selectColumns.get(0).equals("*")) {
      for (String key : row.keySet()) {
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

  private Object resolveColumn(Row row, String columnName) {
    // 列名は大文字小文字を無視して照合する（schema.getColumn と挙動を揃える）
    for (String key : row.keySet()) {
      if (key.equalsIgnoreCase(columnName)) return row.get(key);
    }
    String suffix = "." + columnName;
    Object found = null;
    int count = 0;
    for (String key : row.keySet()) {
      if (endsWithIgnoreCase(key, suffix)) {
        found = row.get(key);
        count++;
      }
    }
    if (count == 1) return found;
    if (count > 1) throw new IllegalArgumentException("Ambiguous column name: " + columnName);
    throw new IllegalArgumentException("Unknown column: " + columnName);
  }

  private boolean endsWithIgnoreCase(String value, String suffix) {
    int offset = value.length() - suffix.length();
    return offset >= 0 && value.regionMatches(true, offset, suffix, 0, suffix.length());
  }
}
