package customdb.chapter07.Parser;

import java.util.List;

import customdb.chapter07.DB.Schema.Column;

public interface Statement {
  record CreateTable(String tableName, List<Column> columns) implements Statement {}

  record Insert(String tableName, List<String> columnNames, List<String> values)
      implements Statement {}

  record Select(
      List<String> selectColumns, String tableName, Condition whereCondition, JoinClause joinClause)
      implements Statement {}

  record Update(String tableName, String columnName, String value, Condition whereCondition)
      implements Statement {}

  record Delete(String tableName, Condition whereCondition) implements Statement {}

  record Condition(String left, String operator, String right) {}

  record JoinClause(String tableName, Condition onCondition) {}
}
