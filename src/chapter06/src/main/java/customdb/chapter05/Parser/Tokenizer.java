package customdb.chapter05.Parser;

import java.util.ArrayList;
import java.util.List;

public class Tokenizer {
  public List<String> tokenize(String sql) {
    List<String> tokens = new ArrayList<>();
    StringBuilder currentToken = new StringBuilder();

    boolean inString = false;

    for (int i = 0; i < sql.length(); i++) {
      char c = sql.charAt(i);

      if (c == '\'') {
        inString = !inString;
        currentToken.append(c);
        continue;
      }

      if (inString) {
        currentToken.append(c);
        continue;
      }

      if (Character.isWhitespace(c)) {
        flush(currentToken, tokens);
      } else if (c == '(' || c == ')' || c == ',') {
        flush(currentToken, tokens);
        tokens.add(String.valueOf(c));
      } else if (c == ';') {
        flush(currentToken, tokens);
        // セミコロンはSQLの終端として扱うため、tokenには入れない
      } else if (c == '=' || c == '<' || c == '>' || c == '!') {
        flush(currentToken, tokens);

        if (i + 1 < sql.length() && sql.charAt(i + 1) == '=') {
          tokens.add(String.valueOf(c) + "=");
          i++;
        } else {
          tokens.add(String.valueOf(c));
        }
      } else {
        currentToken.append(c);
      }
    }

    flush(currentToken, tokens);

    if (inString) {
      throw new IllegalArgumentException("String literal is not closed.");
    }

    return tokens;
  }

  private void flush(StringBuilder currentToken, List<String> tokens) {
    if (currentToken.length() > 0) {
      tokens.add(currentToken.toString());
      currentToken.setLength(0);
    }
  }
}
