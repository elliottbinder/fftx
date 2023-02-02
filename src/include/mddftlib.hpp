using namespace fftx;


static constexpr auto mddft_script{
R"(
var_1:= var("var_1", BoxND([0,0,0], TReal));
var_2:= var("var_2", BoxND(szcube, TReal));
var_3:= var("var_3", BoxND(szcube, TReal));
var_2:= X;
var_3:= Y;
symvar := var("sym", TPtr(TReal));
transform := TFCall(TDecl(TDAG([
        TDAGNode(TTensorI(MDDFT(szcube,sign),1,APar, APar), var_3,var_2),
                ]),
        [var_1]
        ),
    rec(fname:=name, params:= [symvar])
);
)"};


class MDDFTProblem: public FFTXProblem {
public:
    using FFTXProblem::FFTXProblem;
    void randomProblemInstance() {
    }
    void semantics() {
        std::cout << "szcube := [" << sizes.at(0) << ", " << sizes.at(1) << ", " << sizes.at(2) << "];" << std::endl;
        std::cout << "sign := " << sizes.at(3) << ";" << std::endl;
        if(sizes.at(3) == -1)
            std::cout << "prefix := \"fftx_mddft_\";" << std::endl;
        else
            std::cout << "prefix := \"fftx_imddft_\";" << std::endl;
        std::cout << "name := \"transform_spiral\";" << std::endl;
        std::cout << mddft_script << std::endl;
    }
};
