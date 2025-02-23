from typing import Callable, List, Dict, Optional, Union, Any
import numpy as np
from dataclasses import dataclass
from .bridge import EmbeddingBridge, ComparisonResult
import logging

logger = logging.getLogger(__name__)

@dataclass
class ModelInfo:
    """Information about an embedding model"""
    name: str
    provider: str
    dimensions: int
    max_tokens: Optional[int] = None
    cost_per_1k_tokens: Optional[float] = None
    typical_performance: Optional[Dict[str, float]] = None

@dataclass
class MigrationReport:
    """Comprehensive migration analysis report"""
    quality_metrics: ComparisonResult
    performance_metrics: Dict[str, float]
    operational_metrics: Dict[str, Any]
    recommendations: List[str]
    warnings: List[str]

class EmbeddingMigrationTool:
    """High-level API for comparing and migrating between embedding models"""
    
    def __init__(self):
        self.bridge = EmbeddingBridge()
        self._test_suites = self._load_test_suites()
    
    def _load_test_suites(self) -> Dict[str, List[str]]:
        """Load standard test suites for different domains"""
        return {
            "general": [
                "The quick brown fox jumps over the lazy dog",
                "Machine learning is a subset of artificial intelligence",
                "Python is a popular programming language"
            ],
            "technical": [
                "Kubernetes is a container orchestration platform",
                "REST APIs use HTTP methods like GET and POST",
                "SQL is used for querying relational databases"
            ],
            "medical": [
                "Myocardial infarction is commonly known as a heart attack",
                "The patient presents with acute abdominal pain",
                "MRI scans use magnetic fields and radio waves"
            ]
        }
    
    def compare_models(
        self,
        model_a: Callable[[Union[str, List[str]]], np.ndarray],
        model_b: Callable[[Union[str, List[str]]], np.ndarray],
        model_a_info: ModelInfo,
        model_b_info: ModelInfo,
        test_data: Optional[List[str]] = None,
        domain: str = "general"
    ) -> MigrationReport:
        """
        Compare two embedding models and generate a comprehensive migration report
        
        Args:
            model_a: Function that generates embeddings for the first model
            model_b: Function that generates embeddings for the second model
            model_a_info: Metadata about the first model
            model_b_info: Metadata about the second model
            test_data: Optional custom test data
            domain: Domain for standard test suite if test_data not provided
        
        Returns:
            MigrationReport with comprehensive analysis
        """
        # Use provided test data or load from test suite
        texts = test_data or self._test_suites.get(domain, self._test_suites["general"])
        
        try:
            # Generate embeddings
            logger.info(f"Generating embeddings using {model_a_info.name}")
            embeddings_a = self._generate_embeddings(model_a, texts)
            
            logger.info(f"Generating embeddings using {model_b_info.name}")
            embeddings_b = self._generate_embeddings(model_b, texts)
            
            # Compare embeddings using C library
            quality_metrics = self.bridge.compare_embeddings(
                embeddings_a,
                embeddings_b,
                k_neighbors=5
            )
            
            # Compute performance metrics
            performance_metrics = self._compute_performance_metrics(
                model_a, model_b,
                model_a_info, model_b_info,
                texts
            )
            
            # Assess operational impact
            operational_metrics = self._assess_operational_impact(
                model_a_info,
                model_b_info
            )
            
            # Generate recommendations and warnings
            recommendations, warnings = self._generate_insights(
                quality_metrics,
                performance_metrics,
                operational_metrics,
                model_a_info,
                model_b_info
            )
            
            return MigrationReport(
                quality_metrics=quality_metrics,
                performance_metrics=performance_metrics,
                operational_metrics=operational_metrics,
                recommendations=recommendations,
                warnings=warnings
            )
        
        except Exception as e:
            logger.error(f"Error comparing models: {str(e)}")
            raise
    
    def _generate_embeddings(
        self,
        model: Callable[[Union[str, List[str]]], np.ndarray],
        texts: List[str]
    ) -> np.ndarray:
        """Generate embeddings for a list of texts"""
        try:
            embeddings = model(texts)
            if not isinstance(embeddings, np.ndarray):
                embeddings = np.array(embeddings)
            return embeddings
        except Exception as e:
            logger.error(f"Error generating embeddings: {str(e)}")
            raise
    
    def _compute_performance_metrics(
        self,
        model_a: Callable,
        model_b: Callable,
        model_a_info: ModelInfo,
        model_b_info: ModelInfo,
        texts: List[str]
    ) -> Dict[str, float]:
        """Compute performance metrics for both models"""
        import time
        
        metrics = {}
        
        # Measure embedding generation time
        start_time = time.time()
        _ = self._generate_embeddings(model_a, texts)
        model_a_time = time.time() - start_time
        
        start_time = time.time()
        _ = self._generate_embeddings(model_b, texts)
        model_b_time = time.time() - start_time
        
        metrics["speed_ratio"] = model_b_time / model_a_time
        
        # Cost comparison if available
        if model_a_info.cost_per_1k_tokens and model_b_info.cost_per_1k_tokens:
            metrics["cost_ratio"] = (
                model_b_info.cost_per_1k_tokens /
                model_a_info.cost_per_1k_tokens
            )
        
        return metrics
    
    def _assess_operational_impact(
        self,
        model_a_info: ModelInfo,
        model_b_info: ModelInfo
    ) -> Dict[str, Any]:
        """Assess operational impact of migration"""
        return {
            "dimension_change": model_b_info.dimensions != model_a_info.dimensions,
            "provider_change": model_b_info.provider != model_a_info.provider,
            "max_tokens_change": (
                model_b_info.max_tokens != model_a_info.max_tokens
                if model_a_info.max_tokens and model_b_info.max_tokens
                else None
            )
        }
    
    def _generate_insights(
        self,
        quality_metrics: ComparisonResult,
        performance_metrics: Dict[str, float],
        operational_metrics: Dict[str, Any],
        model_a_info: ModelInfo,
        model_b_info: ModelInfo
    ) -> tuple[List[str], List[str]]:
        """Generate recommendations and warnings based on metrics"""
        recommendations = []
        warnings = []
        
        # Quality checks
        if quality_metrics.cosine_similarity < 0.85:
            warnings.append(
                f"Low semantic similarity ({quality_metrics.cosine_similarity:.2f}). "
                "Migration may significantly impact search results."
            )
        
        # Performance checks
        if performance_metrics.get("speed_ratio", 1) > 1.2:
            warnings.append(
                f"New model is {(performance_metrics['speed_ratio']-1)*100:.0f}% slower. "
                "Consider performance impact."
            )
        
        # Cost analysis
        if "cost_ratio" in performance_metrics:
            cost_change = (performance_metrics["cost_ratio"] - 1) * 100
            if cost_change < 0:
                recommendations.append(
                    f"Cost savings of {-cost_change:.0f}% with new model."
                )
            else:
                warnings.append(
                    f"Cost increase of {cost_change:.0f}% with new model."
                )
        
        # Operational impacts
        if operational_metrics["dimension_change"]:
            warnings.append(
                "Dimension change requires reindexing and may impact storage requirements."
            )
        
        if operational_metrics["provider_change"]:
            recommendations.append(
                "Provider change requires updating API credentials and error handling."
            )
        
        return recommendations, warnings 