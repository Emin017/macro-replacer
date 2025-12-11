module top_module(input clk, input wen, input [3:0] d, output [3:0] q);
  OLD_MACRO u_inst (
    .CLK(clk),
    .CW(wen),
    .D(d),
    .Q(q)
  );
endmodule
