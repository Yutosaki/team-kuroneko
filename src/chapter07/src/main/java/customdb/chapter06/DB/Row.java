package customdb.chapter06.DB;

import java.util.LinkedHashMap;
import java.util.Map;
import java.util.Set;

public class Row {
  private final Map<String, Object> values = new LinkedHashMap<>();

  public void put(String columnName, Object value) {
    values.put(columnName, value);
  }

  public Object get(String columnName) {
    return values.get(columnName);
  }

  public boolean contains(String columnName) {
    return values.containsKey(columnName);
  }

  public Set<String> keySet() {
    return values.keySet();
  }

  public Map<String, Object> getValues() {
    return values;
  }

  public void putAll(Row other) {
    values.putAll(other.values);
  }

  @Override
  public String toString() {
    return values.toString();
  }

  @Override
  public boolean equals(Object obj) {
    if (this == obj) {
      return true;
    }

    if (!(obj instanceof Row other)) {
      return false;
    }

    return values.equals(other.values);
  }

  @Override
  public int hashCode() {
    return values.hashCode();
  }
}
