#include <fmt/format.h>

#include "utils.hpp"
#include "layers/activation.hpp"

bool activationLayer(nvinfer1::ITensor *&output, int index, std::string_view activation, nvinfer1::ITensor *input,
										 nvinfer1::INetworkDefinition *network, const std::string &name)
{
	bool success{};
	std::string layer_name;

	if(activation == "linear")
	{
		output = input;
	}
	else if(activation == "relu")
	{
		nvinfer1::IActivationLayer *relu = network->addActivation(*input, nvinfer1::ActivationType::kRELU);
		layer_name = fmt::format("relu_{}", index);

		if(!relu)
		{
			TADS_ERR_MSG_V("Could not initialize %s", layer_name.c_str());
			goto done;
		}

		relu->setName(layer_name.c_str());
		output = relu->getOutput(0);
	}
	else if(activation == "sigmoid" || activation == "logistic")
	{
		nvinfer1::IActivationLayer *sigmoid = network->addActivation(*input, nvinfer1::ActivationType::kSIGMOID);
		layer_name = fmt::format("sigmoid_{}{}", name, index);

		if(!sigmoid)
		{
			TADS_ERR_MSG_V("Could not initialize %s", layer_name.c_str());
			goto done;
		}

		sigmoid->setName(layer_name.c_str());
		output = sigmoid->getOutput(0);
	}
	else if(activation == "tanh")
	{
		nvinfer1::IActivationLayer *tanh = network->addActivation(*input, nvinfer1::ActivationType::kTANH);
		layer_name = fmt::format("tanh_{}{}", name, index);

		if(!tanh)
		{
			TADS_ERR_MSG_V("Could not initialize %s", layer_name.c_str());
			goto done;
		}

		tanh->setName(layer_name.c_str());
		output = tanh->getOutput(0);
	}
	else if(activation == "leaky")
	{
		nvinfer1::IActivationLayer *leaky = network->addActivation(*input, nvinfer1::ActivationType::kLEAKY_RELU);
		layer_name = fmt::format("leaky_{}{}", name, index);

		if(!leaky)
		{
			TADS_ERR_MSG_V("Could not initialize %s", layer_name.c_str());
			goto done;
		}

		leaky->setName(layer_name.c_str());
		leaky->setAlpha(0.1);
		output = leaky->getOutput(0);
	}
	else if(activation == "softplus")
	{
		nvinfer1::IActivationLayer *softplus = network->addActivation(*input, nvinfer1::ActivationType::kSOFTPLUS);
		layer_name = fmt::format("softplus_{}{}", name, index);

		if(!softplus)
		{
			TADS_ERR_MSG_V("Could not initialize %s", layer_name.c_str());
			goto done;
		}

		softplus->setName(layer_name.c_str());
		output = softplus->getOutput(0);
	}
	else if(activation == "mish")
	{
		nvinfer1::IActivationLayer *softplus = network->addActivation(*input, nvinfer1::ActivationType::kSOFTPLUS);
		layer_name = fmt::format("softplus_{}{}", name, index);

		if(!softplus)
		{
			TADS_ERR_MSG_V("Could not initialize %s", layer_name.c_str());
			goto done;
		}

		softplus->setName(layer_name.c_str());
		nvinfer1::IActivationLayer *tanh = network->addActivation(*softplus->getOutput(0), nvinfer1::ActivationType::kTANH);
		layer_name = fmt::format("tanh_{}{}", name, index);

		if(!tanh)
		{
			TADS_ERR_MSG_V("Could not initialize %s", layer_name.c_str());
			goto done;
		}

		tanh->setName(layer_name.c_str());
		nvinfer1::IElementWiseLayer *mish =
				network->addElementWise(*input, *tanh->getOutput(0), nvinfer1::ElementWiseOperation::kPROD);
		layer_name = fmt::format("mish_{}{}", name, index);

		if(!mish)
		{
			TADS_ERR_MSG_V("Could not initialize %s", layer_name.c_str());
			goto done;
		}

		mish->setName(layer_name.c_str());
		output = mish->getOutput(0);
	}
	else if(activation == "silu" || activation == "swish")
	{
		nvinfer1::IActivationLayer *sigmoid = network->addActivation(*input, nvinfer1::ActivationType::kSIGMOID);
		layer_name = fmt::format("sigmoid_{}{}", name, index);

		if(sigmoid == nullptr)
		{
			TADS_ERR_MSG_V("Could not initialize %s", layer_name.c_str());
			goto done;
		}

		sigmoid->setName(layer_name.c_str());
		nvinfer1::IElementWiseLayer *silu =
				network->addElementWise(*input, *sigmoid->getOutput(0), nvinfer1::ElementWiseOperation::kPROD);
		layer_name = fmt::format("silu_{}{}", name, index);

		if(!silu)
		{
			TADS_ERR_MSG_V("Could not initialize %s", layer_name.c_str());
			goto done;
		}

		silu->setName(layer_name.c_str());
		output = silu->getOutput(0);
	}
	else if(activation == "hardsigmoid")
	{
		nvinfer1::IActivationLayer *hard_sigmoid = network->addActivation(*input, nvinfer1::ActivationType::kHARD_SIGMOID);
		layer_name = fmt::format("hardsigmoid_{}{}", name, index);

		if(!hard_sigmoid)
		{
			TADS_ERR_MSG_V("Could not initialize %s", layer_name.c_str());
			goto done;
		}

		hard_sigmoid->setName(layer_name.c_str());
		hard_sigmoid->setAlpha(1.0 / 6.0);
		hard_sigmoid->setBeta(0.5);
		output = hard_sigmoid->getOutput(0);
	}
	else if(activation == "hardswish")
	{
		nvinfer1::IActivationLayer *hard_sigmoid = network->addActivation(*input, nvinfer1::ActivationType::kHARD_SIGMOID);
		layer_name = fmt::format("hardsigmoid_{}{}", name, index);

		if(!hard_sigmoid)
		{
			TADS_ERR_MSG_V("Could not initialize %s", layer_name.c_str());
			goto done;
		}

		hard_sigmoid->setName(layer_name.c_str());
		hard_sigmoid->setAlpha(1.0 / 6.0);
		hard_sigmoid->setBeta(0.5);
		nvinfer1::IElementWiseLayer *hardswish =
				network->addElementWise(*input, *hard_sigmoid->getOutput(0), nvinfer1::ElementWiseOperation::kPROD);
		layer_name = fmt::format("hardswish_{}{}", name, index);

		if(!hardswish)
		{
			TADS_ERR_MSG_V("Could not initialize %s", layer_name.c_str());
			goto done;
		}

		hardswish->setName(layer_name.c_str());
		output = hardswish->getOutput(0);
	}
	else
	{
		TADS_ERR_MSG_V("Activation not supported: %s", activation.data());
		goto done;
	}

	success = true;

done:
	if(!success)
	{
		TADS_ERR_MSG_V("%s failed", __func__);
	}
	return success;
}