package customdb.chapter07.Operator;

import customdb.chapter07.DB.Row;
import customdb.chapter07.Parser.Statement.Condition;
import java.io.IOException;

public class FilterOperator implements Operator {
  private final Operator child;
  private final Condition condition;

  public FilterOperator(Operator child, Condition condition) {
    this.child = child;
    this.condition = condition;
  }

  @Override
  public void open() throws IOException {
    child.open();
  }

  @Override
  public Row next() throws IOException {
    while (true) {
      Row row = child.next();
      if (row == null) {
        return null;
      }
      if (ConditionEvaluator.matches(row, condition)) {
        return row;
      }
    }
  }

  @Override
  public void close() throws IOException {
    child.close();
  }
}
