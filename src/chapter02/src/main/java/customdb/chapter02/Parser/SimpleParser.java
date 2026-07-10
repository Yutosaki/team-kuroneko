package customdb.chapter02.Parser;

public class SimpleParser {

  public SimpleParser() {}

  public String[] parse(String sql) {
    String[] tokens = sql.trim().split("\\s+");
    tokens[0] = tokens[0].toLowerCase();
    return tokens;
  }

  public String getCommand(String[] tokens) {
    return tokens.length > 0 ? tokens[0] : "";
  }
}
