package customdb.chapter06.Parser;

import java.util.ArrayList;
import java.util.List;

import customdb.chapter06.DB.Schema;

public class SimpleParser {
  private final Tokenizer tokenizer;
  private static final int CONDITION_TOKEN_COUNT = 3;

  public SimpleParser() {
    this.tokenizer = new Tokenizer();
  }

  public Statement parseStatement(String sql) {
    List<String> tokens = tokenizer.tokenize(sql);

    if (tokens.isEmpty()) {
      throw new IllegalArgumentException("SQL is empty.");
    }

    String command = tokens.get(0).toLowerCase();

    return switch (command) {
      case "select" -> parseSelect(tokens);
      case "insert" -> parseInsert(tokens);
      case "create" -> parseCreateTable(tokens);
      case "update" -> parseUpdate(tokens);
      case "delete" -> parseDelete(tokens);
      default -> throw new IllegalArgumentException("Unsupported SQL command: " + command);
    };
  }

  private Statement.Select parseSelect(List<String> tokens) {
    expect(tokens, 0, "select");

    List<String> selectColumns = new ArrayList<>();
    int index = 1;

    // from句が来るまでcolumnsを取得する
    while (index < tokens.size() && !equalsIgnoreCase(tokens.get(index), "from")) {
      if (!tokens.get(index).equals(",")) {
        selectColumns.add(tokens.get(index));
      }
      index++;
    }

    if (selectColumns.isEmpty()) {
      throw new IllegalArgumentException("SELECT columns are missing.");
    }

    expect(tokens, index, "from");
    index++;

    if (index >= tokens.size()) {
      throw new IllegalArgumentException("Table name is missing after FROM.");
    }

    String tableName = tokens.get(index);
    index++;

    Statement.JoinClause joinClause = null;
    Statement.Condition whereCondition = null;

    // JOIN句が来る場合はJOIN句を取得する
    if (index < tokens.size() && equalsIgnoreCase(tokens.get(index), "join")) {
      index++;

      if (index >= tokens.size()) {
        throw new IllegalArgumentException("Table name is missing after JOIN.");
      }

      String joinTableName = tokens.get(index);
      index++;

      expect(tokens, index, "on");
      index++;

      Statement.Condition onCondition = parseCondition(tokens, index);
      index += CONDITION_TOKEN_COUNT;

      joinClause = new Statement.JoinClause(joinTableName, onCondition);
    }

    // WHERE句が来る場合はWHERE句を取得する
    if (index < tokens.size() && equalsIgnoreCase(tokens.get(index), "where")) {
      index++;

      whereCondition = parseCondition(tokens, index);
      index += CONDITION_TOKEN_COUNT;
    }

    if (index < tokens.size()) {
      throw new IllegalArgumentException(
          "Unexpected token in SELECT statement: " + tokens.get(index));
    }

    return new Statement.Select(selectColumns, tableName, whereCondition, joinClause);
  }

  private Statement.Insert parseInsert(List<String> tokens) {
    // insert into tablename (column1, column2, ...) values (value1, value2, ...);
    // insert into tablename values (value1, value2, ...);
    expect(tokens, 0, "insert");
    expect(tokens, 1, "into");

    if (tokens.size() < 4) {
      throw new IllegalArgumentException("Invalid INSERT statement.");
    }

    String tableName = tokens.get(2);
    int index = 3;

    List<String> columnNames = new ArrayList<>();
    // 「(」が来る場合はカラム名を取得する
    if (index < tokens.size() && tokens.get(index).equals("(")) {
      index++;

      while (index < tokens.size() && !tokens.get(index).equals(")")) {
        if (!tokens.get(index).equals(",")) {
          columnNames.add(tokens.get(index));
        }
        index++;
      }

      expect(tokens, index, ")");
      index++;
    }

    expect(tokens, index, "values");
    index++;

    expect(tokens, index, "(");
    index++;

    List<String> values = new ArrayList<>();

    // 「)」が来るまで値を取得する
    while (index < tokens.size() && !tokens.get(index).equals(")")) {
      if (!tokens.get(index).equals(",")) {
        // 文字列リテラルのクォートを削除して値を格納する
        values.add(removeQuote(tokens.get(index)));
      }
      index++;
    }

    expect(tokens, index, ")");
    index++;

    if (index < tokens.size()) {
      throw new IllegalArgumentException(
          "Unexpected token in INSERT statement: " + tokens.get(index));
    }

    return new Statement.Insert(tableName, columnNames, values);
  }

  private Statement.CreateTable parseCreateTable(List<String> tokens) {
    // create table tablename (column1 type1, column2 type2, ...);
    expect(tokens, 0, "create");
    expect(tokens, 1, "table");

    if (tokens.size() < 5) {
      throw new IllegalArgumentException("Invalid CREATE TABLE statement.");
    }

    String tableName = tokens.get(2);
    int index = 3;

    expect(tokens, index, "(");
    index++;

    List<Schema.Column> columns = new ArrayList<>();

    // 「)」が来るまでcolumnを取得する
    while (index < tokens.size() && !tokens.get(index).equals(")")) {
      String columnName = tokens.get(index);
      index++;

      if (index >= tokens.size()) {
        throw new IllegalArgumentException("Column type is missing: " + columnName);
      }

      Schema.DataType type = parseDataType(tokens.get(index));
      index++;

      int length = 0;

      // string型の場合は長さを指定する必要がある
      if (index < tokens.size() && tokens.get(index).equals("(")) {
        index++;

        if (index >= tokens.size()) {
          throw new IllegalArgumentException("Length is missing: " + columnName);
        }

        length = Integer.parseInt(tokens.get(index));
        index++;

        expect(tokens, index, ")");
        index++;
      }

      columns.add(new Schema.Column(columnName, type, length));

      if (index < tokens.size() && tokens.get(index).equals(",")) {
        index++;
      }
    }

    expect(tokens, index, ")");
    index++;

    if (index < tokens.size()) {
      throw new IllegalArgumentException(
          "Unexpected token in CREATE TABLE statement: " + tokens.get(index));
    }

    return new Statement.CreateTable(tableName, columns);
  }

  private Statement.Update parseUpdate(List<String> tokens) {
    expect(tokens, 0, "update");

    if (tokens.size() < 6) {
      throw new IllegalArgumentException("Invalid UPDATE statement.");
    }

    String tableName = tokens.get(1);
    int index = 2;

    expect(tokens, index, "set");
    index++;

    String columnName = tokens.get(index);
    index++;

    expect(tokens, index, "=");
    index++;

    String value = tokens.get(index);
    index++;

    Statement.Condition whereCondition = null;

    if (index < tokens.size() && equalsIgnoreCase(tokens.get(index), "where")) {
      index++;
      whereCondition = parseCondition(tokens, index);
      index += CONDITION_TOKEN_COUNT;
    }

    if (index < tokens.size()) {
      throw new IllegalArgumentException(
          "Unexpected token in UPDATE statement: " + tokens.get(index));
    }

    return new Statement.Update(tableName, columnName, value, whereCondition);
  }

  private Statement.Delete parseDelete(List<String> tokens) {
    expect(tokens, 0, "delete");
    expect(tokens, 1, "from");

    if (tokens.size() < 3) {
      throw new IllegalArgumentException("Invalid DELETE statement.");
    }

    String tableName = tokens.get(2);
    int index = 3;

    Statement.Condition whereCondition = null;

    if (index < tokens.size() && equalsIgnoreCase(tokens.get(index), "where")) {
      index++;
      whereCondition = parseCondition(tokens, index);
      index += CONDITION_TOKEN_COUNT;
    }

    if (index < tokens.size()) {
      throw new IllegalArgumentException(
          "Unexpected token in DELETE statement: " + tokens.get(index));
    }

    return new Statement.Delete(tableName, whereCondition);
  }

  private Statement.Condition parseCondition(List<String> tokens, int index) {
    if (index + 2 >= tokens.size()) {
      throw new IllegalArgumentException("Invalid condition.");
    }

    String left = tokens.get(index);
    String operator = tokens.get(index + 1);
    String right = tokens.get(index + 2);

    return new Statement.Condition(left, operator, right);
  }

  private void expect(List<String> tokens, int index, String expected) {
    if (index >= tokens.size()) {
      throw new IllegalArgumentException("Expected '" + expected + "', but reached end of SQL.");
    }

    if (!equalsIgnoreCase(tokens.get(index), expected)) {
      throw new IllegalArgumentException(
          "Expected '" + expected + "', but found '" + tokens.get(index) + "'.");
    }
  }

  private boolean equalsIgnoreCase(String actual, String expected) {
    return actual.equalsIgnoreCase(expected);
  }

  private String removeQuote(String value) {
    if (value.length() >= 2 && value.startsWith("'") && value.endsWith("'")) {
      return value.substring(1, value.length() - 1);
    }

    return value;
  }

  private Schema.DataType parseDataType(String type) {
    return switch (type.toLowerCase()) {
      case "int", "integer" -> Schema.DataType.INTEGER;
      case "float" -> Schema.DataType.FLOAT;
      case "double" -> Schema.DataType.DOUBLE;
      case "varchar", "string" -> Schema.DataType.STRING;
      default -> throw new IllegalArgumentException("Unsupported data type: " + type);
    };
  }
}
