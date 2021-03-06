//
// Created by Adam Zvada on 17.06.17.
//

#include "ast.h"


//--------------------------------------------------------------------------------


// core LLVM data struct
static LLVMContext * context;

// helper object for generating LLVM instructions
static IRBuilder<> * builder;

// LLVM construct that contains function and global variables
static Module * module;

// symtable of code
static std::map<std::string, Value *> namedValues;
static SymboleTable * symboleTable;

// Break Block
static BasicBlock * breakTarget;

//--------------------------------------------------------------------------------

void llvmAstInit(LLVMContext & theContext, Module * theModule, IRBuilder<> & theBuilder, BasicBlock * breakTargetBlock, SymboleTable * symbTable) {
    context = &theContext;
    module = theModule;
    builder = &theBuilder;
    breakTarget = breakTargetBlock;
    symboleTable = symbTable;
}

void SetGlobalVariables() {

    for (auto var : symboleTable->GetAllGlobalVar()) {
        GlobalVariable * globalVar;

        if (var->type == VAR || var->type == CONST) {
            Constant * varInteger = ConstantInt::get(Type::getInt32Ty(*context), var->value, true);
            if (var->type == CONST) {
                globalVar = new GlobalVariable(*module, Type::getInt32Ty(*context), true, GlobalValue::ExternalLinkage,
                                               0, var->ident);
            } else if (var->type == VAR) {
                globalVar = new GlobalVariable(*module, Type::getInt32Ty(*context), false, GlobalValue::ExternalLinkage,
                                               0, var->ident);
            }
            globalVar->setInitializer(varInteger);
        } else if (var->type == ARRAY) {
            ArrayType * arrayType = ArrayType::get(builder->getInt32Ty(), abs(var->start - var->end));
            globalVar = new GlobalVariable(*module, arrayType, false, GlobalValue::ExternalLinkage, 0, var->ident);
            ConstantAggregateZero* init = ConstantAggregateZero::get(arrayType);
            globalVar->setInitializer(init);
        }

        globalVar->setAlignment(4);
        namedValues[var->ident] = module->getGlobalVariable(var->ident);
    }
}

//TODO LOCAL VAR

//-------------------------------CODEGEN-------------------------------------------------


Value * Var::GenerateIR() {
    cout << "Creating variable value: " << _name << " " << _value << endl;

    Value * var = namedValues[_name];

    if (!var) {
        std::cout << "Variable with idenifier \"" << _name << "was not decleared." << std::endl;
    }

    if (_rvalue) {
        return builder->CreateLoad(var, _name.c_str());
    }

    return var;
}

Value * VarArray::GenerateIR() {

    Value * arr = namedValues[_name];

    Value * index = _index->GenerateIR();

    vector<Value *> indexList = {ConstantInt::get(builder->getInt32Ty(),0), index};
    Value * ptr = builder->CreateGEP(arr, indexList, _name.c_str()); //GetElementPointer

    if (_rvalue) {
        return builder->CreateLoad(ptr, (_name + "i").c_str());
    } else {
        return ptr;
    }
}

Value * Numb::GenerateIR() {
    std::cout << "Creating integer value: " << _value << std::endl;

    return ConstantInt::get(Type::getInt32Ty(*context), _value, true);
}

Value * BinOp::GenerateIR() {
    std::cout << "Performing binary operation " << _op << std::endl;

    llvm::Value * left = _left->GenerateIR();
    llvm::Value * right = _right->GenerateIR();

    if (!left || !right) {
        std::cout << "Error on left or right operand" << std::endl;
        return nullptr;
    }

    switch (_op) {
        case PLUS:
            return builder->CreateAdd(left, right, "addtmp");
        case MINUS:
            return builder->CreateSub(left, right, "subtmp");
        case MULTIPLY:
            return builder->CreateMul(left, right, "multmp");
        case DIVIDE:
            return builder->CreateSDiv(left, right, "divtmp");
        case LESS:
            return builder->CreateICmpSLT(left, right, ".lttmp");
        case LESS_OR_EQ:
            return builder->CreateICmpSLE(left, right, ".ltetmp");
        case GRATHER:
            return builder->CreateICmpSGT(left, right, ".gttmp");
        case GRATHER_OR_EQ:
            return builder->CreateICmpSGE(left, right, ".gtetmp");
        case EQ:
            return builder->CreateICmpEQ(left, right, ".eqtmp");
        case NOT_EQ:
            return builder->CreateICmpNE(left, right, ".neqtmp");
        case kwOR:
            return builder->CreateOr(left, right, ".ortmp");
        case kwAND:
            return builder->CreateAnd(left, right, ".andtmp");
        case kwMOD:
            return builder->CreateSRem(left, right, ".modtmp");
        default:
            std::cout << "Unsupported OP, should not be evaluated" << std::endl;
            return nullptr;
    }
}

Value * UnaryMinus::GenerateIR() {
    std::cout << "Creating negative value" << std::endl;
    Value * expr = _expression->GenerateIR();
    if (!expr) {
        std::cout << "Error on expression for unanry minus" << std::endl;
    }
    return builder->CreateNeg(_expression->GenerateIR(), "minus");
}

Value * StatmList::GenerateIR() {

    if (!_statement) {
        cout << "No execuate block" << endl;
        return nullptr;
    }

    StatmList * curStatementList = this;

    while(curStatementList->_next) {
        curStatementList->_statement->GenerateIR();
        curStatementList = curStatementList->_next;
    }
    if(curStatementList->_statement) {
        curStatementList->_statement->GenerateIR();
    }


    return Constant::getNullValue(Type::getInt32Ty(*context));
}

Value * Assign::GenerateIR() {

    int * value = new int(0);
    string ident = _var->GetName();
    SymboleType type = symboleTable->GetConstOrVar(ident, value);
    if (type == CONST) {
        cout << "Cannot assign to CONST variable" << endl;
    }

    Value * expr = _expr->GenerateIR();
    Value * var = _var->GenerateIR();

    return builder->CreateStore(expr, var);
}

Value * Read::GenerateIR() {

    Value * var = _var->GenerateIR();

    vector<Value*> scanVal;
    scanVal.push_back(builder->CreateGlobalStringPtr("%d"));
    scanVal.push_back(var);

    return builder->CreateCall(scanfFunc(), scanVal, "scanfCall");
}

Value * Write::GenerateIR() {

    Value * expr = _expression->GenerateIR();

    vector<Value*> printVal;
    printVal.push_back(builder->CreateGlobalStringPtr("%d\n"));
    printVal.push_back(expr);

    return builder->CreateCall(printFunc(), printVal, "printfCall");
}

Value * If::GenerateIR() {

    Value * condition = _condition->GenerateIR();
    if (!condition) {
        return nullptr;
    }

    Function * mainFunc = builder->GetInsertBlock()->getParent();

    BasicBlock * thenBlock = llvm::BasicBlock::Create(*context, "then", mainFunc);
    BasicBlock * elseBlock = llvm::BasicBlock::Create(*context, "else");
    BasicBlock * conditionBlock = llvm::BasicBlock::Create(*context, "ifcont");

    builder->CreateCondBr(condition, thenBlock, elseBlock);

    builder->SetInsertPoint(thenBlock);

    Value * thenVal = _then->GenerateIR();

    builder->CreateBr(conditionBlock);

    thenBlock = builder->GetInsertBlock();

    mainFunc->getBasicBlockList().push_back(elseBlock);
    builder->SetInsertPoint(elseBlock);

    if (_else) {
        Value *elseVal = _else->GenerateIR();
    }

    builder->CreateBr(conditionBlock);

    elseBlock = builder->GetInsertBlock();

    mainFunc->getBasicBlockList().push_back(conditionBlock);
    builder->SetInsertPoint(conditionBlock);

    return conditionBlock;
}

Value * While::GenerateIR() {

    Value * condition = _condition->GenerateIR();
    if (!condition) {
        return nullptr;
    }

    Function * mainFunc = builder->GetInsertBlock()->getParent();

    BasicBlock * loopBlock = BasicBlock::Create(*context, "loop", mainFunc);
    BasicBlock * afterBlock = BasicBlock::Create(*context, "after");
    BasicBlock * prevTarget = breakTarget;
    breakTarget = afterBlock;

    builder->CreateCondBr(condition, loopBlock, afterBlock);

    builder->SetInsertPoint(loopBlock);

    _statement->GenerateIR();

    condition = _condition->GenerateIR();
    builder->CreateCondBr(condition, loopBlock, afterBlock);
    mainFunc->getBasicBlockList().push_back(afterBlock);

    builder->SetInsertPoint(afterBlock);

    breakTarget = prevTarget;

    return nullptr;
}

//Value * For::GenerateIR() {
//
//    Function *TheFunction = builder->GetInsertBlock()->getParent();
//
//    BasicBlock *condBB = BasicBlock::Create(*context, "condblock", TheFunction);
//    BasicBlock *LoopBB = BasicBlock::Create(*context, "loop", TheFunction);
//    BasicBlock *nextBlock = BasicBlock::Create(*context, "afterloop", TheFunction);
//
//    /* init */
//    _initAssign->GenerateIR();
//    builder->CreateBr(condBB);
//
//    /* condition */
//    builder->SetInsertPoint(condBB);
//
//    Value *limitV = _to->GenerateIR();
//
//    Var *var = dynamic_cast<Assign *>(_initAssign)->GetVar();
//
//    Value *condV;
//    if (!_isAscending)
//        condV = builder->CreateICmpSGE(var->GenerateIR(), limitV, "le");
//    else
//        condV = builder->CreateICmpSLE(var->GenerateIR(), limitV, "ge");
//
//    builder->CreateCondBr(condV, LoopBB, nextBlock);
//
//
//    builder->SetInsertPoint(LoopBB);
//    _body->Translate();
//
//    /* iterate */
//    Numb *one = new Numb(1);
//    Value *updated = BinOp(_isAscending ? MINUS : PLUS, var, one).GenerateIR();
//    //builder->CreateStore(updated, var);
//
//    builder->CreateBr(condBB);
//
//    /* next */
//    builder->SetInsertPoint(nextBlock);
//
//
//    return Constant::getNullValue(Type::getInt32Ty(*context));
//}

Value * Break::GenerateIR() {

    Function * mainFunc = builder->GetInsertBlock()->getParent();

    BasicBlock* breakBlock = BasicBlock::Create(*context, "break", mainFunc);
    BasicBlock* afterBlock = BasicBlock::Create(*context, "aftbreak");

    builder->CreateBr(breakBlock);
    builder->SetInsertPoint(breakBlock);
    if(breakTarget){
        builder->CreateBr(breakTarget);
    }

    mainFunc->getBasicBlockList().push_back(afterBlock);
    builder->SetInsertPoint(afterBlock);

    return nullptr;
}

Value * Prog::GenerateIR() {

    SetGlobalVariables();

    return _statmentList->GenerateIR();
}


//--------------------------------------------------------------------------------

Constant * Write::printFunc() {
    vector<Type *> args;
    args.push_back(Type::getInt8PtrTy(*context));
    FunctionType *printfType = FunctionType::get(builder->getInt32Ty(), args, true);
    return module->getOrInsertFunction("printf", printfType);
}

Constant * Read::scanfFunc() {
    std::vector<Type *> args;
    args.push_back(Type::getInt8PtrTy(*context));
    FunctionType *scanfType = FunctionType::get(builder->getInt32Ty(), args, true);
    return module->getOrInsertFunction("scanf", scanfType);
}

