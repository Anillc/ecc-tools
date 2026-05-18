module top (
  input clk,
  output sink
);

  wire leaf;

  BUF_X1 u_buf (
    .A(clk),
    .Y(leaf)
  );

  BUF_X1 u_leaf (
    .A(leaf),
    .Y(sink)
  );

endmodule
